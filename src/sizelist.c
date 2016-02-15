/*
  Copyright 2012-2016 Jyri J. Virkki <jyri@virkki.com>

  This file is part of dupd.

  dupd is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  dupd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with dupd.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "filecompare.h"
#include "hashlist.h"
#include "main.h"
#include "md5.h"
#include "paths.h"
#include "sizelist.h"
#include "stats.h"
#include "utils.h"

static struct size_list * size_list_head;
static struct size_list * size_list_tail;
static int reader_continue = 1;

struct size_list {
  off_t size;
  char * path_list;
  int state;
  int fully_read;
  off_t bytes_read;
  pthread_mutex_t lock;
  struct size_list * next;
};

// Size list states.
#define SLS_NEED_BYTES_ROUND_1 88
#define SLS_READY_1 89
#define SLS_NEED_BYTES_ROUND_2 90
#define SLS_READY_2 91
#define SLS_NEEDS_ROUND_3 92
#define SLS_DONE 94


/** ***************************************************************************
 * Create a new size list node. A node contains one file size and points
 * to the head of the path list of files which are of this size.
 *
 * Parameters:
 *    size      - Size
 *    path_list - Head of path list of files of this size
 *
 * Return: An intialized/allocated size list node.
 *
 */
static struct size_list * new_size_list_entry(off_t size, char * path_list)
{
  struct size_list * e = (struct size_list *)malloc(sizeof(struct size_list));
  e->size = size;
  e->path_list = path_list;
  e->state = SLS_NEED_BYTES_ROUND_1;
  e->fully_read = 0;
  e->bytes_read = 0;
  if (pthread_mutex_init(&e->lock, NULL)) {
                                                             // LCOV_EXCL_START
    printf("error: new_size_list_entry mutex init failed!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP
  e->next = NULL;
  return e;
}


/** ***************************************************************************
 * Free all content buffers in a path list.
 *
 * Parameters:
 *    entry - First entry of this path list (not its head).
 *
 * Return: none
 *
 */
static void free_path_list_buffers(char * entry)
{
  char * buffer;

  while (entry != NULL) {
    buffer = pl_entry_get_buffer(entry);
    if (buffer != NULL) {
      free(buffer);
      pl_entry_set_buffer(entry, NULL);
    }
    entry = pl_entry_get_next(entry);
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void add_to_size_list(off_t size, char * path_list)
{
  if (size < 0) {
    printf("add_to_size_list: bad size! %ld\n", size);       // LCOV_EXCL_START
    dump_path_list("bad size", size, path_list);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  stats_size_list_avg = stats_size_list_avg +
    ( (size - stats_size_list_avg) / (stats_size_list_count + 1) );

  if (size_list_head == NULL) {
    size_list_head = new_size_list_entry(size, path_list);
    size_list_tail = size_list_head;
    stats_size_list_count = 1;
    return;
  }

  size_list_tail->next = new_size_list_entry(size, path_list);
  size_list_tail = size_list_tail->next;
  stats_size_list_count++;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void process_size_list(sqlite3 * dbh)
{
  if (size_list_head == NULL) {
    return;
  }

  char * line;
  char * node;
  char * path_list_head;
  int count = 0;
  int round_one_hash_blocks = 1;

  struct size_list * size_node = size_list_head;

  while (size_node != NULL) {
    count++;
    path_list_head = size_node->path_list;

    uint32_t path_count = pl_get_path_count(path_list_head);

    node = pl_get_first_entry(path_list_head);

    if (verbosity >= 2) {
      if (verbosity >= 4) { printf("\n"); }
      printf("Processing %d/%d (%d files of size %ld)\n",
             count, stats_size_list_count, path_count, size_node->size);
      if (size_node->size < 0) {
        printf("Or not, since size makes no sense!\n");       // LCOV_EXCL_LINE
        exit(1);                                              // LCOV_EXCL_LINE
      }
    }

    // If we only have two files of this size, compare them directly
    if (opt_compare_two && path_count == 2) {
      char * path1 = pl_entry_get_path(node);
      char * path2 = pl_entry_get_path(pl_entry_get_next(node));

      compare_two_files(dbh, path1, path2, size_node->size);
      stats_two_file_compare++;
      goto CONTINUE;
    }

    // If we only have three files of this size, compare them directly
    if (opt_compare_three && path_count == 3) {
      char * path1 = pl_entry_get_path(node);
      char * node2 = pl_entry_get_next(node);
      char * path2 = pl_entry_get_path(node2);
      char * path3 = pl_entry_get_path(pl_entry_get_next(node2));

      compare_three_files(dbh, path1, path2, path3, size_node->size);
      stats_three_file_compare++;
      goto CONTINUE;
    }

    // If we have files of smallish size, do a full hash up front
    if (size_node->size <= (hash_one_block_size * hash_one_max_blocks)) {
      stats_full_hash_first++;
      round_one_hash_blocks = 0;
      if (verbosity >= 4) {
        printf("Computing full hash up front, file size is small enough.\n");
      }
    } else {
      stats_one_block_hash_first++;
      round_one_hash_blocks = 1;
    }

    // Build initial hash list for these files
    stats_set_round_one++;
    struct hash_list * hl_one = get_hash_list(HASH_LIST_ONE);
    do {
      line = pl_entry_get_path(node);
      add_hash_list(hl_one, line, round_one_hash_blocks,
                    hash_one_block_size, 0);
      node = pl_entry_get_next(node);
    } while (node != NULL);

    if (verbosity >= 4) { printf("Done building first hash list.\n"); }
    if (verbosity >= 6) {
      printf("Contents of hash list hl_one:\n");
      print_hash_list(hl_one);
    }

    if (save_uniques) {
      skim_uniques(dbh, hl_one, save_uniques);
    }

    // If no potential dups after this round, we're done!
    if (HASH_LIST_NO_DUPS(hl_one)) {
      if (verbosity >= 4) { printf("No potential dups left, done!\n"); }
      stats_set_no_dups_round_one++;
      goto CONTINUE;
    }

    // If by now we already have a full hash, publish and we're done!
    if (round_one_hash_blocks == 0) {
      stats_set_dups_done_round_one++;
      if (verbosity >= 4) { printf("Some dups confirmed, here they are:\n"); }
      publish_duplicate_hash_list(dbh, hl_one, size_node->size);
      goto CONTINUE;
    }

    struct hash_list * hl_previous = hl_one;

    // Do filtering pass with intermediate number of blocks, if configured
    if (intermediate_blocks > 1) {
      if (verbosity >= 4) {
        printf("Don't know yet. Filtering to second hash list\n");
      }
      stats_set_round_two++;
      struct hash_list * hl_partial = get_hash_list(HASH_LIST_PARTIAL);
      hl_previous = hl_partial;
      filter_hash_list(hl_one, intermediate_blocks,
                       hash_block_size, hl_partial, 0);

      if (verbosity >= 6) {
        printf("Contents of hash list hl_partial:\n");
        print_hash_list(hl_partial);
      }

      if (save_uniques) {
        skim_uniques(dbh, hl_partial, save_uniques);
      }

      // If no potential dups after this round, we're done!
      if (HASH_LIST_NO_DUPS(hl_partial)) {
        if (verbosity >= 4) { printf("No potential dups left, done!\n"); }
        stats_set_no_dups_round_two++;
        goto CONTINUE;
      }

      // If this size < hashed so far, we're done so publish to db
      if (size_node->size < hash_block_size * intermediate_blocks) {
        stats_set_dups_done_round_two++;
        if (verbosity >= 4) {
          printf("Some dups confirmed, here they are:\n");
        }
        publish_duplicate_hash_list(dbh, hl_partial, size_node->size);
        goto CONTINUE;
      }
    }

    // for anything left, do full file hash
    if (verbosity >= 4) {
      printf("Don't know yet. Filtering to full hash list\n");
    }
    stats_set_full_round++;
    struct hash_list * hl_full = get_hash_list(HASH_LIST_FULL);
    filter_hash_list(hl_previous, 0, hash_block_size,
                     hl_full, intermediate_blocks);
    if (verbosity >= 6) {
      printf("Contents of hash list hl_full:\n");
      print_hash_list(hl_full);
    }

    if (save_uniques) {
      skim_uniques(dbh, hl_full, save_uniques);
    }

    // If no potential dups after this round, we're done!
    if (HASH_LIST_NO_DUPS(hl_full)) {
      if (verbosity >= 4) { printf("No potential dups left, done!\n"); }
      stats_set_no_dups_full_round++;
      goto CONTINUE;
    }

    // Still something left, go publish them to db
    if (verbosity >= 4) {
      printf("Finally some dups confirmed, here they are:\n");
    }
    stats_set_dups_done_full_round++;
    publish_duplicate_hash_list(dbh, hl_full, size_node->size);

    CONTINUE:
    size_node = size_node->next;
  }
}


/** ***************************************************************************
 * Read bytes from disk for the reader thread in states
 * SLS_NEED_BYTES_ROUND_1 and SLS_NEED_BYTES_ROUND_2.
 *
 * Bytes are read to a buffer allocated for each path node. If a prior buffer
 * is present (in round 2 from round 1), free it first.
 *
 * Parameters:
 *    size_node   - Head of the path list of files to read.
 *    max_to_read - Read this many bytes from each file in this path list,
 *                  unless the file is smaller in which case mark it as
 *                  entirely read.
 *
 * Return: none
 *
 */
static void reader_read_bytes(struct size_list * size_node, off_t max_to_read)
{
  static char * spaces = "                                        [reader] ";
  char * node;
  char * path;
  char * buffer;
  ssize_t received;

  node = pl_get_first_entry(size_node->path_list);

  if (size_node->size <= max_to_read) {
    size_node->bytes_read = size_node->size;
    size_node->fully_read = 1;
  } else {
    size_node->bytes_read = max_to_read;
    size_node->fully_read = 0;
  }

  do {
    path = pl_entry_get_path(node);

    // If the buffer of this node is not NULL, it is the buffer
    // used in a previous round for this path, so free it first
    buffer = pl_entry_get_buffer(node);
    if (buffer != NULL) {
      free(buffer);
    }

    // The path may be null if this particular path within this pathlist
    // has been discarded as a potential duplicate already. If so, skip.
    if (path[0] != 0) {
      buffer = (char *)malloc(size_node->bytes_read);
      received = read_file_bytes(path, buffer, size_node->bytes_read, 0);
      if (received != size_node->bytes_read) {
        printf("error: read %zd bytes from [%s] but wanted %ld\n",
               received, path, size_node->bytes_read);
        size_node->bytes_read = 0;
      }
      if (thread_verbosity >= 2) {
        printf("%s%ld bytes from %s\n",spaces,size_node->bytes_read,path);
      }
      pl_entry_set_buffer(node, buffer);

    } else {
      pl_entry_set_buffer(node, NULL);
    }

    node = pl_entry_get_next(node);

  } while (node != NULL);
}


/** ***************************************************************************
 * Reader thread main function.
 *
 * Loops through the size list, looking for size entries which need bytes
 * read from disk (SLS_NEED_BYTES_ROUND_1 or SLS_NEED_BYTES_ROUND_2)
 * and then reads the needed bytes and saves them in the path list.
 *
 * Thread exits when reader_continue is set to false by the main thread.
 *
 * Parameters:
 *    arg - Not used.
 *
 * Return: none
 *
 */
static void * reader_main(void * arg)
{
  (void)arg;
  char * spaces = "                                        [reader] ";
  struct size_list * size_node;
  int loops = 0;
  off_t max_to_read;

  if (thread_verbosity) {
    printf("%sthread created\n", spaces);
  }

  do {
    size_node = size_list_head;
    loops++;
    if (thread_verbosity) {
      printf("%sStarting size list loop #%d\n", spaces, loops);
    }

    do {
      pthread_mutex_lock(&size_node->lock);

      if (thread_verbosity >= 2) {
        printf("%s(loop %d) size:%ld state:%d\n",
               spaces, loops, size_node->size, size_node->state);
      }

      switch(size_node->state) {

      case SLS_NEED_BYTES_ROUND_1:
        max_to_read = hash_one_block_size * hash_one_max_blocks;
        reader_read_bytes(size_node, max_to_read);
        size_node->state = SLS_READY_1;
        break;

      case SLS_NEED_BYTES_ROUND_2:
        max_to_read = hash_block_size * intermediate_blocks;
        reader_read_bytes(size_node, max_to_read);
        size_node->state = SLS_READY_2;
        break;
      }

      pthread_mutex_unlock(&size_node->lock);
      size_node = size_node->next;

    } while (size_node != NULL && reader_continue);

  } while (reader_continue);

  if (thread_verbosity) {
    printf("%sDONE (%d loops)\n", spaces, loops);
  }

  return(NULL);
}


/** ***************************************************************************
 * Helper function to process files from a size node to a hash list.
 * Used for rounds 1 and 2 during hash list processing.
 *
 * Parameters:
 *    dbh                          - db handle for saving duplicates/uniques
 *    size_node                    - Process this size node (and associated
 *                                   path list.
 *    hl                           - Save paths to this hash list.
 *    counter_round_full           - Increased if files read completely.
 *    counter_round_partial        - Increased if files partially read.
 *    counter_no_dups_this_round   - Increased if no dups established.
 *    counter_dupd_done_this_round - Increased if dups found.
 *
 * Return: 1 if this path list was completed (either confirmed duplicates
 *         or ruled out possibility of being duplicates).
 *
 */
static int build_hash_list_round(sqlite3 * dbh,
                                 struct size_list * size_node,
                                 struct hash_list * hl,
                                 int * counter_round_full,
                                 int * counter_round_partial,
                                 int * counter_no_dups_this_round,
                                 int * counter_dupd_done_this_round)
{
  char * node;
  char * path;
  char * buffer;
  int completed = 0;

  node = pl_get_first_entry(size_node->path_list);

  // For small files, they may have been fully read already
  if (size_node->fully_read) { (*counter_round_full)++; }
  else { (*counter_round_partial)++; }

  // Build hash list for these files
  do {
    path = pl_entry_get_path(node);

    // The path may be null if this particular path within this pathlist
    // has been discarded as a potential duplicate already. If so, skip.
    if (path[0] != 0) {
      buffer = pl_entry_get_buffer(node);
      add_hash_list_from_mem(hl, path, buffer, size_node->bytes_read);
    }

    node = pl_entry_get_next(node);
  } while (node != NULL);

  if (verbosity >= 4) { printf("Done building first hash list.\n"); }
  if (verbosity >= 6) {
    printf("Contents of hash list hl:\n");
    print_hash_list(hl);
  }

  skim_uniques(dbh, hl, save_uniques);

  // If no potential dups after this round, we're done!
  if (HASH_LIST_NO_DUPS(hl)) {
    if (verbosity >= 4) { printf("No potential dups left, done!\n"); }
    (*counter_no_dups_this_round)++;
    size_node->state = SLS_DONE;
    completed = 1;
    free_path_list_buffers(pl_get_first_entry(size_node->path_list));

  } else {
    // If by now we already have a full hash, publish and we're done!
    if (size_node->fully_read) {
      (*counter_dupd_done_this_round)++;
      if (verbosity >= 4) { printf("Some dups confirmed, here they are:\n"); }
      publish_duplicate_hash_list(dbh, hl, size_node->size);
      size_node->state = SLS_DONE;
      completed = 1;
      free_path_list_buffers(pl_get_first_entry(size_node->path_list));
    }
  }

  return completed;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void threaded_process_size_list(sqlite3 * dbh)
{
  static char * spaces = "[main] ";
  struct size_list * size_node;
  struct hash_list * hl;
  pthread_t reader_thread;
  int work_remains;
  int did_one;
  int loops = 0;
  int round = 0;
  int count = 0;
  uint32_t path_count = 0;
  char * node;
  char * path;

  if (size_list_head == NULL) {
    return;
  }

  // Start my companion thread which will read bytes from disk for me
  if (pthread_create(&reader_thread, NULL, reader_main, NULL)) {
                                                             // LCOV_EXCL_START
    printf("error: unable to create sizelist reader thread!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if (thread_verbosity) {
    printf("%sStarting...\n", spaces);
    usleep(10000);
  }

  // Then start going through the size list, processing size entries which
  // have data available.

  do {
    size_node = size_list_head;
    loops++;
    work_remains = 0;
    did_one = 0;

    if (thread_verbosity) {
      printf("%sStarting size list loop #%d\n", spaces, loops);
    }

    do {
      pthread_mutex_lock(&size_node->lock);

      if (thread_verbosity >= 2) {
        printf("%s(loop %d) size:%ld state:%d\n",
               spaces, loops, size_node->size, size_node->state);
      }

      switch(size_node->state) {

      case SLS_NEED_BYTES_ROUND_1:
      case SLS_NEED_BYTES_ROUND_2:
        work_remains = 1;
        break;

      case SLS_READY_1:
        hl = get_hash_list(HASH_LIST_ONE);
        stats_set_round_one++;

        did_one = build_hash_list_round(dbh, size_node, hl,
                                        &stats_full_hash_first,
                                        &stats_one_block_hash_first,
                                        &stats_set_no_dups_round_one,
                                        &stats_set_dups_done_round_one);
        count += did_one;
        round = 1;

        if (!did_one) {
          if (intermediate_blocks > 1) {
            size_node->state = SLS_NEED_BYTES_ROUND_2;
            work_remains = 1;
          } else {
            size_node->state = SLS_NEEDS_ROUND_3;
          }
        }
        break;

      case SLS_READY_2:
        hl = get_hash_list(HASH_LIST_PARTIAL);
        stats_set_round_two++;

        did_one = build_hash_list_round(dbh, size_node, hl,
                                        &stats_full_hash_second,
                                        &stats_partial_hash_second,
                                        &stats_set_no_dups_round_two,
                                        &stats_set_dups_done_round_two);
        count += did_one;
        round = 2;
        if (!did_one) {
          size_node->state = SLS_NEEDS_ROUND_3;
        }
        break;
      }

      if (did_one) {
        if (verbosity >= 2) {
          path_count = pl_get_path_count(size_node->path_list);
          if (verbosity == 2) {
            printf("Processed %d/%d (%d files of size %ld)\n", count,
                   stats_size_list_count, path_count, size_node->size);
          } else {
            printf("Processed %d/%d (%d files of size %ld) (loop %d) "
                   "(round %d)\n", count, stats_size_list_count, path_count,
                   size_node->size, loops, round);
          }
        }
      }

      pthread_mutex_unlock(&size_node->lock);
      size_node = size_node->next;

    } while (size_node != NULL);

  } while (work_remains);

  if (thread_verbosity) {
    printf("%sDONE (%d loops)\n", spaces, loops);
  }

  reader_continue = 0;
  pthread_join(reader_thread, NULL);

  if (thread_verbosity) {
    printf("%sjoined reader thread\n", spaces);
  }

  // Only entries remaining in the size_list are those marked SLS_NEEDS_ROUND_3
  // These will need to be processed in this thread directly.

  size_node = size_list_head;

  do {

    did_one = 0;
    switch(size_node->state) {

    case SLS_NEEDS_ROUND_3:
      node = pl_get_first_entry(size_node->path_list);
      uint32_t path_count = pl_get_path_count(size_node->path_list);

      // If we only have two files of this size, compare them directly
      if (opt_compare_two && path_count == 2) {
        char * path1 = pl_entry_get_path(node);
        char * path2 = pl_entry_get_path(pl_entry_get_next(node));

        compare_two_files(dbh, path1, path2, size_node->size);
        stats_two_file_compare++;
        size_node->state = SLS_DONE;
        did_one = 1;
        break;
      }

      // If we only have three files of this size, compare them directly
      if (opt_compare_three && path_count == 3) {
        char * path1 = pl_entry_get_path(node);
        char * node2 = pl_entry_get_next(node);
        char * path2 = pl_entry_get_path(node2);
        char * path3 = pl_entry_get_path(pl_entry_get_next(node2));

        compare_three_files(dbh, path1, path2, path3, size_node->size);
        stats_three_file_compare++;
        size_node->state = SLS_DONE;
        did_one = 1;
        break;
      }

      // for anything left, do full file hash
      if (verbosity >= 4) {
        printf("Don't know yet. Filtering to full hash list\n");
      }
      stats_set_full_round++;

      struct hash_list * hl_full = get_hash_list(HASH_LIST_FULL);
      do {
        path = pl_entry_get_path(node);

        // The path may be null if this particular path within this pathlist
        // has been discarded as a potential duplicate already. If so, skip.
        if (path[0] != 0) {
          add_hash_list(hl_full, path, 0, hash_block_size, 0);
        }
        node = pl_entry_get_next(node);
      } while (node != NULL);

      if (verbosity >= 6) {
        printf("Contents of hash list hl_full:\n");
        print_hash_list(hl_full);
      }

      if (save_uniques) {
        skim_uniques(dbh, hl_full, save_uniques);
      }

      // If no potential dups after this round, we're done!
      if (HASH_LIST_NO_DUPS(hl_full)) {
        if (verbosity >= 4) { printf("No potential dups left, done!\n"); }
        stats_set_no_dups_full_round++;
        size_node->state = SLS_DONE;
        did_one = 1;
        break;
      }

      // Still something left, go publish them to db
      if (verbosity >= 4) {
        printf("Finally some dups confirmed, here they are:\n");
      }
      stats_set_dups_done_full_round++;
      publish_duplicate_hash_list(dbh, hl_full, size_node->size);
      size_node->state = SLS_DONE;
      did_one = 1;
      break;

    case SLS_DONE:
      break;

    default:
      printf("In final pass, bad sizelist state %d\n", size_node->state);
      exit(1);
    }

    if (did_one) {
      if (verbosity >= 2) {
        count++;
        path_count = pl_get_path_count(size_node->path_list);
        if (verbosity == 2) {
          printf("Processed %d/%d (%d files of size %ld)\n", count,
                 stats_size_list_count, path_count, size_node->size);
        } else {
          printf("Processed %d/%d (%d files of size %ld) "
                 "(loop %d) (round 3)\n", count, stats_size_list_count,
                 path_count, size_node->size, loops);
        }
      }
    }

    size_node = size_node -> next;

  } while (size_node != NULL);

  if (thread_verbosity) {
    printf("%sDONE\n", spaces);
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void init_size_list()
{
  size_list_head = NULL;
  size_list_tail = NULL;
  stats_size_list_count = 0;
  stats_size_list_avg = 0;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void free_size_list()
{
  char * pl;
  char * entry;

  if (size_list_head != NULL) {
    struct size_list * p = size_list_head;
    struct size_list * me = size_list_head;

    while (p != NULL) {
      p = p->next;
      pthread_mutex_destroy(&me->lock);
      pl = me->path_list;
      if (pl != NULL) {
        entry = pl_get_first_entry(pl);
        free_path_list_buffers(entry);
      }
      free(me);
      me = p;
    }
  }
}
