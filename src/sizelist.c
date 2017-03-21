/*
  Copyright 2012-2017 Jyri J. Virkki <jyri@virkki.com>

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
#include "paths.h"
#include "readlist.h"
#include "sizelist.h"
#include "sizetree.h"
#include "stats.h"
#include "utils.h"

static int round1_max_bytes;
static struct size_list * size_list_head;
static struct size_list * size_list_tail;
static struct size_list * deleted_list_head;
static struct size_list * deleted_list_tail;
static int reader_continue = 1;
static int avg_read_time = 0;
static int read_count = 0;
static int open_files = 0;
static int reader_main_hdd_round_2 = 0;
static int round3_info_buffers_used = 0;

static pthread_mutex_t reader_main_hdd_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t reader_main_hdd_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t show_processed_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t r3_loop_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t r3_loop_cond = PTHREAD_COND_INITIALIZER;
static int r3_hasher_done = 0;

// Size list states.
#define SLS_NEED_BYTES_ROUND_1 88
#define SLS_READY_1 89
#define SLS_NEED_BYTES_ROUND_2 90
#define SLS_READY_2 91
#define SLS_NEEDS_ROUND_3 92
#define SLS_DONE 94
#define SLS_R3_HASH_ME 96
#define SLS_R3_HASH_ME_FINAL 97
#define SLS_R3_HASH_DONE 98
#define SLS_R3_READ_MORE 99
#define SLS_R3_READ_FINAL 100
#define SLS_R3_HASHER_IGNORE 101
#define SLS_DELETED 102

#define R3_BUFSIZE (1024 * 256)
#define R3_MAX_SETS 4
#define R3_MAX_OPEN_FILES 4

struct round3_info {
  int state;
  int fd;
  off_t read_from;
  size_t read_count;
  void * hash_ctx;
  char hash_result[HASH_MAX_BUFSIZE];
  int bufsize;
  char * buffer;
};


/** ***************************************************************************
 * For debug output, return name of state constants.
 *
 */
static char * state_name(int state)
{
  switch(state) {
  case SLS_NEED_BYTES_ROUND_1: return "SLS_NEED_BYTES_ROUND_1";
  case SLS_READY_1: return "SLS_READY_1";
  case SLS_NEED_BYTES_ROUND_2: return "SLS_NEED_BYTES_ROUND_2";
  case SLS_READY_2: return "SLS_READY_2";
  case SLS_NEEDS_ROUND_3: return "SLS_NEEDS_ROUND_3";
  case SLS_DONE: return "SLS_DONE";
  case SLS_R3_HASH_ME: return "SLS_R3_HASH_ME";
  case SLS_R3_HASH_ME_FINAL: return "SLS_R3_HASH_ME_FINAL";
  case SLS_R3_HASH_DONE: return "SLS_R3_HASH_DONE";
  case SLS_R3_READ_MORE: return "SLS_R3_READ_MORE";
  case SLS_R3_READ_FINAL: return "SLS_R3_READ_FINAL";
  case SLS_R3_HASHER_IGNORE: return "SLS_R3_HASHER_IGNORE";
  case SLS_DELETED: return "SLS_DELETED";
  default: return "*** UNKNOWN ***";
  }
}


/** ***************************************************************************
 * Print total sets processed.
 *
 */
static inline void show_processed_done(int total)
{
  LOG(L_PROGRESS,
      "Done processing %d sets                             \n", total);
}


/** ***************************************************************************
 * Print progress on set processing.
 *
 */
static inline void show_processed(int total, int files, long size,
                                  int loop, char phase)
{
  d_mutex_lock(&show_processed_lock, "show_processed");

  stats_size_list_done++;

  LOG(L_PROGRESS, "Processed %d/%d [%c] (%d files of size %ld) (loop %d)\n",
      stats_size_list_done, total, phase, files, size, loop);

  if (stats_size_list_done > total) {                        // LCOV_EXCL_START
    printf("\nThat's not right...\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  d_mutex_unlock(&show_processed_lock);
}


/** ***************************************************************************
 * Remove 'entry' from the size list by pointing previous->next to entry->next.
 * 'entry' is then placed onto the deleted_size_list to be freed later.
 *
 * The caller MUST hold lock on 'entry', if this is called during a
 * multi-threaded stage. The lock is RELEASED here.
 *
 */
static void unlink_size_list_entry(struct size_list * entry,
                                   struct size_list * previous)
{
  d_mutex_lock(&previous->lock, "unlink entry, lock previous");

  if (entry->next == NULL) {
    previous->next = NULL;
  } else {
    d_mutex_lock(&entry->next->lock, "unlink entry, lock next");
    previous->next = entry->next;
    d_mutex_unlock(&entry->next->lock);
  }

  LOG(L_MORE_THREADS, "Removing size list entry of size %ld\n",
      (long)entry->size);

  entry->state = SLS_DELETED;
  entry->next = NULL;
  entry->dnext = NULL;

  d_mutex_unlock(&entry->lock);
  d_mutex_unlock(&previous->lock);

  // The reason entry is added to a deleted list instead of just freed here
  // is that the other thread might be waiting on entry->lock right about now
  // so we can't just destroy the mutex here. Instead, let it be and point
  // entry->next to null so it doesn't continue walking that way.

  if (deleted_list_tail == NULL) {
    deleted_list_head = entry;
    deleted_list_tail = entry;
  } else {
    deleted_list_tail->dnext = entry;
    deleted_list_tail = entry;
  }
}


/** ***************************************************************************
 * Returns a struct for round3_info OR NULL if none available right now.
 *
 * Parameters: none
 *
 * Return: round3_info buffer or NULL.
 *
 */
static inline struct round3_info * allocate_round3_info()
{
  if (round3_info_buffers_used <= R3_MAX_OPEN_FILES) {
    round3_info_buffers_used++;
    struct round3_info * p =
      (struct round3_info *)malloc(sizeof(struct round3_info));
    p->buffer = (char *)malloc(R3_BUFSIZE);
    return p;
  }

  return NULL;
}


/** ***************************************************************************
 * Release a round3_info buffer. Does NOT free the outer struct.
 *
 * Parameters:
 *    info - Release the buffer from this struct
 *
 * Return: None.
 *
 */
static inline void release_round3_info_buffer(struct round3_info * info)
{
  round3_info_buffers_used--;
  free(info->buffer);
  info->buffer = NULL;
}


/** ***************************************************************************
 * Release a round3_info buffer.
 *
 * Parameters:
 *    info - Free this round3_info struct.
 *
 * Return: None.
 *
 */
static inline void free_round3_info(struct round3_info * info)
{
  free(info);
}


/** ***************************************************************************
 * Hashes incoming file data during round 3.
 * Round 3 covers large files which have not been discarded yet, so these
 * are read in blocks by the reader thread and hashed here.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
static void * round3_hasher(void * arg)
{
  sqlite3 * dbh = (sqlite3 *)arg;
  struct size_list * size_node;
  struct size_list * size_node_next;
  struct round3_info * status;
  uint32_t path_count = 0;
  int every_hash_computed;
  int loops = 0;
  int done;
  int entry_changed;
  int sets;
  int loop_buf_init;
  int loop_partial_hash;
  int loop_hash_completed;
  int loop_set_processed;
  char * entry;
  char * path;
  int set_count;
  char * self = "                                        [r3-hasher] ";

  pthread_setspecific(thread_name, self);
  LOG(L_THREADS, "thread created\n");

  do {
    size_node = size_list_head;
    loops++;
    done = 1;
    sets = R3_MAX_SETS;
    set_count = 1;
    loop_buf_init = 0;
    loop_partial_hash = 0;
    loop_hash_completed = 0;
    loop_set_processed = 0;

    do {

      d_mutex_lock(&size_node->lock, "r3-hasher top");

      // SLS_DELETED means we managed to grab a node that has been moved
      // to the deleted list, so there's nothing to do with it.
      if (size_node->state == SLS_DELETED) {
        done = 0;
        goto R3H_NEXT_NODE;
      }

      int count = pl_get_path_count(size_node->path_list);
      if ( (opt_compare_two && count == 2) ||
           (opt_compare_three && count == 3) ) {
        goto R3H_NEXT_NODE;
      }

      status = NULL;
      off_t size = size_node->size;

      LOG_THREADS {
        int count = pl_get_path_count(size_node->path_list);
        LOG(L_THREADS,
            "SET %d (%d files of size %ld) (loop %d) state: %s\n",
            set_count++, count, (long)size, loops,
            state_name(size_node->state));
      }

      switch(size_node->state) {

      case SLS_DONE:
      case SLS_R3_HASHER_IGNORE:
        break;

      case SLS_NEEDS_ROUND_3:
        done = 0;
        every_hash_computed = 1;

        entry = pl_get_first_entry(size_node->path_list);

        while (entry != NULL) {
          entry_changed = 0;
          status = (struct round3_info *)pl_entry_get_buffer(entry);
          path = pl_entry_get_path(entry);

          LOG_MORE_THREADS {
            if (status == NULL) {
              LOG(L_MORE_THREADS, "   entry state: NULL [%s]\n", path);
            } else {
              LOG(L_MORE_THREADS, "   entry state: %s [%s]\n",
                  state_name(status->state), path);
            }
          }

          if (status == NULL) {

            // If path is empty this is a discarded entry
            if (path[0] != 0) {
              every_hash_computed = 0;
              status = allocate_round3_info();
              if (status == NULL) {
                sets = 0;
                goto R3H_NEXT_NODE;
              }
              loop_buf_init++;

              pl_entry_set_buffer(entry, (char *)status);
              status->read_from = 0;
              if (size < R3_BUFSIZE) {
                status->read_count = size;
                status->state = SLS_R3_READ_FINAL;
              } else {
                status->read_count = R3_BUFSIZE;
                status->state = SLS_R3_READ_MORE;
              }
              status->bufsize = status->read_count;
              status->hash_ctx = hash_fn_buf_init();
              entry_changed = 1;
            }

          } else {

            switch (status->state) {

            case SLS_R3_HASH_ME:
              every_hash_computed = 0;
              hash_fn_buf_update(status->hash_ctx,
                                 status->buffer, status->bufsize);
              status->read_from = status->read_from + status->bufsize;
              status->read_count = size - status->read_from;
              if (status->read_count > R3_BUFSIZE) {
                status->read_count = R3_BUFSIZE;
                status->state = SLS_R3_READ_MORE;
              } else {
                status->state = SLS_R3_READ_FINAL;
              }
              status->bufsize = status->read_count;
              entry_changed = 1;
              loop_partial_hash++;
              break;

            case SLS_R3_HASH_ME_FINAL:
              hash_fn_buf_final(status->hash_ctx, status->buffer,
                                status->bufsize, status->hash_result);
              status->state = SLS_R3_HASH_DONE;
              free(status->hash_ctx);
              release_round3_info_buffer(status);
              entry_changed = 1;
              loop_hash_completed++;
              break;

            case SLS_R3_READ_MORE:
            case SLS_R3_READ_FINAL:
              every_hash_computed = 0;
              break;

            case SLS_R3_HASH_DONE:
              break;

            default:                                         // LCOV_EXCL_START
              printf("error: impossible inner state %s in round3_hasher!\n",
                     state_name(status->state));
              exit(1);
            }                                                // LCOV_EXCL_STOP
          }

          if (entry_changed) {
            LOG(L_MORE_THREADS, "          => : %s [%s]\n",
                state_name(status->state), path);
          }

          entry = pl_entry_get_next(entry);
        }

        if (every_hash_computed) {

          loop_set_processed++;
          stats_sets_processed[ROUND3]++;
          entry = pl_get_first_entry(size_node->path_list);
          struct hash_list * hl_full = get_hash_list(HASH_LIST_FULL);

          do {
            path = pl_entry_get_path(entry);
            // The path may be null if this particular path within this
            // pathlist has been discarded as a potential duplicate already.
            // If so, skip.
            if (path[0] != 0) {
              status = (struct round3_info *)pl_entry_get_buffer(entry);
              add_to_hash_list(hl_full, path, status->hash_result);
              free_round3_info(status);
              pl_entry_set_buffer(entry, NULL);
            }
            entry = pl_entry_get_next(entry);
          } while (entry != NULL);

          LOG_TRACE {
            LOG(L_TRACE, "Contents of hash list hl_full:\n");
            print_hash_list(hl_full);
          }

          if (save_uniques) {
            skim_uniques(dbh, hl_full, save_uniques);
          }

          // If no potential dups after this round, we're done!
          if (HASH_LIST_NO_DUPS(hl_full)) {

            LOG_TRACE {
              LOG(L_TRACE, "No potential dups left, done!\n");
              LOG(L_TRACE, "Discarded in round 3 the potentials: ");
              entry = pl_get_first_entry(size_node->path_list);
              do {
                path = pl_entry_get_path(entry);
                if (path[0] != 0) {
                  LOG(L_TRACE, "%s ", path);
                }
                entry = pl_entry_get_next(entry);
              } while (entry != NULL);
              LOG(L_TRACE, "\n");
            }

            stats_sets_dup_not[ROUND3]++;
            size_node->state = SLS_DONE;

          } else {
            // Still something left, go publish them to db
            LOG(L_TRACE, "Finally some dups confirmed, here they are:\n");
            stats_sets_dup_done[ROUND3]++;
            publish_duplicate_hash_list(dbh, hl_full, size_node->size);
            size_node->state = SLS_DONE;
          }

          LOG_PROGRESS {
            path_count = pl_get_path_count(size_node->path_list);
          }

          show_processed(stats_size_list_count, path_count,
                         size_node->size, loops, '3');

        } // every_hash_computed

        break;

      default:                                               // LCOV_EXCL_START
        printf("error: impossible state %s in round3_hasher!\n",
               state_name(size_node->state));
        exit(1);
      }                                                      // LCOV_EXCL_STOP

    R3H_NEXT_NODE:
      size_node_next = size_node->next;
      d_mutex_unlock(&size_node->lock);
      size_node = size_node_next;

      if (sets == 0) {
        size_node = NULL;
        done=0;
      }

    } while (size_node != NULL);

    LOG(L_THREADS, "Finished size list loop #%d: init: %d, partial: %d, "
        "final: %d, sets completed: %d\n",
        loops, loop_buf_init, loop_partial_hash,
        loop_hash_completed, loop_set_processed);

    if (only_testing) {
      slow_down(10, 100);
    }

    // If we didn't accomplish anything at all in this loop, let's not
    // try again. Instead, wait until there might be some work.
    if (!done && loop_buf_init == 0 && loop_partial_hash == 0 &&
        loop_hash_completed == 0 && loop_set_processed == 0) {
      d_mutex_lock(&r3_loop_lock, "r3-hasher loop end no work");
      d_cond_signal(&r3_loop_cond);
      LOG(L_THREADS, "Waiting for something to do...\n");
      d_cond_wait(&r3_loop_cond, &r3_loop_lock);
      d_mutex_unlock(&r3_loop_lock);
    } else {
      d_mutex_lock(&r3_loop_lock, "r3-hasher loop end work done");
      d_cond_signal(&r3_loop_cond);
      d_mutex_unlock(&r3_loop_lock);
    }

  } while (!done);

  r3_hasher_done = 1;
  d_mutex_lock(&r3_loop_lock, "r3-hasher all done");
  d_cond_signal(&r3_loop_cond);
  d_mutex_unlock(&r3_loop_lock);

  LOG(L_THREADS, "DONE (%d loops)\n", loops);

  return NULL;
}


/** ***************************************************************************
 * Read from a file which might be open already, for round 3.
 *
 * Parameters:
 *    entry  - path entry being processed
 *    status - status of this file
 *
 * Return: bytes read
 *
 */
static inline ssize_t round3_reader(char * entry, struct round3_info * status)
{
  char * path = pl_entry_get_path(entry);

  // If this is the first read request, file isn't open yet
  if (status->read_from == 0) {
    status->fd = open(path, O_RDONLY);
    if (status->fd < 0) {                                    // LCOV_EXCL_START
      printf("Error opening [%s]\n", path);
      perror(NULL);
      exit(1);
    }                                                        // LCOV_EXCL_STOP

    // We will be reading the entire file in round3
#ifdef FADVISE
    posix_fadvise(status->fd, 0, 0, POSIX_FADV_WILLNEED);
#endif

    open_files++;
  }

  ssize_t got = read(status->fd, status->buffer, status->read_count);
  status->bufsize = got;
  stats_total_bytes_read += got;
  return got;
}


/** ***************************************************************************
 * Process entries in size list which are in the state SLS_NEEDS_ROUND_3.
 *
 * These are analyzed by:
 *  - if 2 files && opt_compare_two then by direct comparison
 *  - if 3 files && opt_compare_three then by direct comparison
 *  - else by hashing each file and comparing hashes
 *
 * Parameters:
 *    dbh         - Database pointer.
 *
 * Return: none
 *
 */
static void process_round_3(sqlite3 * dbh)
{
  struct size_list * size_node;
  struct size_list * previous_size_node;
  struct size_list * next_node;
  struct size_list * next_node_next;
  struct round3_info * status;
  pthread_t hasher_thread;
  uint32_t path_count = 0;
  int did_one;
  int done_something;
  int loops = 0;
  int done;
  int changed;
  int skipped;
  int set_count;
  int free_myself;
  int read_two;
  int read_three;
  int read_partial;
  int read_final;

  char * node;
  char * path = NULL;

  stats_round_start[ROUND3] = get_current_time_millis();

  // Purge sets which are already SLS_DONE by skipping over them so
  // we don't need to look at them again.
  skipped = 0;
  size_node = size_list_head;
  while (size_node != NULL) {
    next_node = size_node->next;
    while (next_node != NULL && next_node->state == SLS_DONE) {
      next_node_next = next_node->next;
      pthread_mutex_destroy(&next_node->lock);
      free(next_node);
      skipped++;
      next_node = next_node_next;
    }
    size_node->next = next_node;
    size_node = size_node->next;
  }

  LOG(L_INFO, "Purged %d size list entries in DONE state\n", skipped);

  // Start my companion hasher thread
  if (pthread_create(&hasher_thread, NULL, round3_hasher, dbh)) {
                                                             // LCOV_EXCL_START
    printf("error: unable to create round3_hasher thread!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  usleep(1000);

  do {
    size_node = size_list_head;
    previous_size_node = NULL;
    done = 1;
    loops++;
    set_count = 1;
    skipped = 0;
    done_something = 0;
    read_two = 0;
    read_three = 0;
    read_partial = 0;
    read_final = 0;

    do {
      did_one = 0;
      free_myself = 0;
      d_mutex_lock(&size_node->lock, "r3-reader top");
      path_count = pl_get_path_count(size_node->path_list);

      LOG_THREADS {
        off_t size = size_node->size;
        LOG(L_THREADS,
            "SET %d (%d files of size %ld) (loop %d) state: %s\n",
            set_count++, path_count, (long)size, loops,
            state_name(size_node->state));
      }

      switch(size_node->state) {

      case SLS_NEEDS_ROUND_3:
        done = 0;
        node = pl_get_first_entry(size_node->path_list);

        // If we only have two files of this size, compare them directly
        if (opt_compare_two && path_count == 2) {
          char * node1 = pl_entry_get_valid_node(node);
          char * path1 = pl_entry_get_path(node1);
          char * node2 = pl_entry_get_valid_node(pl_entry_get_next(node1));
          char * path2 = pl_entry_get_path(node2);

          compare_two_files(dbh, path1, path2, size_node->size);
          stats_two_file_compare++;
          size_node->state = SLS_DONE;
          did_one = 1;
          done_something = 1;
          read_two++;
          break;
        }

        // If we only have three files of this size, compare them directly
        if (opt_compare_three && path_count == 3) {
          char * node1 = pl_entry_get_valid_node(node);
          char * path1 = pl_entry_get_path(node1);
          char * node2 = pl_entry_get_valid_node(pl_entry_get_next(node1));
          char * path2 = pl_entry_get_path(node2);
          char * node3 = pl_entry_get_valid_node(pl_entry_get_next(node2));
          char * path3 = pl_entry_get_path(node3);

          compare_three_files(dbh, path1, path2, path3, size_node->size);
          stats_three_file_compare++;
          size_node->state = SLS_DONE;
          did_one = 1;
          done_something = 1;
          read_three++;
          break;
        }

        while (node != NULL) {
          status = (struct round3_info *)pl_entry_get_buffer(node);
          changed = 0;
          skipped = 0;

          path = pl_entry_get_path(node);

          LOG_MORE_THREADS {
            if (status == NULL) {
              LOG(L_MORE_THREADS, "   entry state: NULL [%s]\n", path);
            } else {
              LOG(L_MORE_THREADS, "   entry state: %s @%ld [%s]\n",
                  state_name(status->state), (long)status->read_from, path);
            }
          }

          // While walking the list, when we reach an entry with no status
          // (and valid path), it means we're past the max number of read
          // buffers, so might as well skip to the end of the list.
          if (status == NULL && path != NULL && path[0] != 0) {
            d_mutex_unlock(&size_node->lock);
            size_node = NULL;
            goto PR3_NEXT_NODE;
          }

          if (status != NULL) {

            switch (status->state) {

            case SLS_R3_READ_MORE:
              round3_reader(node, status);
              status->state = SLS_R3_HASH_ME;
              changed = 1;
              done_something = 1;
              read_partial++;
              break;

            case SLS_R3_READ_FINAL:
              round3_reader(node, status);
              close(status->fd);
              open_files--;
              status->fd = 0;
              status->state = SLS_R3_HASH_ME_FINAL;
              changed = 1;
              done_something = 1;
              read_final++;
              break;

            case SLS_R3_HASH_ME:
            case SLS_R3_HASH_ME_FINAL:
            case SLS_R3_HASH_DONE:
              break;

            default:                                         // LCOV_EXCL_START
              printf("error: impossible inner state %d in process_round3!\n",
                     status->state);
              exit(1);
            }                                                // LCOV_EXCL_STOP
          }

          if (changed) {
            LOG(L_MORE_THREADS, "          => : %s [%s]\n",
                state_name(status->state), path);
          }

          if (skipped) {
            node = NULL;
          } else {
            node = pl_entry_get_next(node);
          }

        }

        break;

      case SLS_DONE:
        if (previous_size_node != NULL) {
          free_myself = 1;
        }
        break;

      default:                                               // LCOV_EXCL_START
        printf("In final pass, bad sizelist state %d\n", size_node->state);
        exit(1);
      }                                                      // LCOV_EXCL_STOP

      if (did_one) {
        LOG_PROGRESS {
          path_count = pl_get_path_count(size_node->path_list);
        }
        show_processed(stats_size_list_count, path_count,
                       size_node->size, loops, '3');
      }

      next_node_next = size_node->next;

      if (free_myself) {
        unlink_size_list_entry(size_node, previous_size_node);
        size_node = NULL;
      } else {
        previous_size_node = size_node;
        d_mutex_unlock(&size_node->lock);
      }

      size_node = next_node_next;

    PR3_NEXT_NODE:
      LOG(L_MORE_TRACE, "end of path loop");

    } while (size_node != NULL);

    LOG(L_THREADS, "Finished size list loop #%d: R2:%d R3:%d Rp:%d Rf:%d\n",
        loops, read_two, read_three, read_partial, read_final);

    if (only_testing) {
      slow_down(10, 100);
    }

    // Let the hasher thread know I did something.
    if (done_something) {
      d_mutex_lock(&r3_loop_lock, "r3-reader loop end work done");
      d_cond_signal(&r3_loop_cond);
      d_mutex_unlock(&r3_loop_lock);
    } else {
      if (!r3_hasher_done) {
        d_mutex_lock(&r3_loop_lock, "r3-reader loop end no work");
        d_cond_signal(&r3_loop_cond);
        LOG(L_THREADS, "Waiting for something to do...\n");
        d_cond_wait(&r3_loop_cond, &r3_loop_lock);
        d_mutex_unlock(&r3_loop_lock);
      }
    }

  } while (!done);


  if (!r3_hasher_done) {
    LOG(L_THREADS, "Waiting for hasher thread...\n");
    d_mutex_lock(&r3_loop_lock, "r3-reader all done");
    d_cond_signal(&r3_loop_cond);
    d_mutex_unlock(&r3_loop_lock);
  }

  d_join(hasher_thread, NULL);

  d_mutex_lock(&r3_loop_lock, "r3-reader end");
  stats_round_duration[ROUND3] =
    get_current_time_millis() - stats_round_start[ROUND3];
  d_mutex_unlock(&r3_loop_lock);

  LOG(L_THREADS, "DONE (%d loops)\n", loops);
}


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
  e->buffers_filled = 0;
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
 * Public function, see header file.
 *
 */
struct size_list * add_to_size_list(off_t size, char * path_list)
{
  if (size < 0) {
    printf("add_to_size_list: bad size! %ld\n", (long)size); // LCOV_EXCL_START
    dump_path_list("bad size", size, path_list);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  stats_size_list_avg = stats_size_list_avg +
    ( (size - stats_size_list_avg) / (stats_size_list_count + 1) );

  if (size_list_head == NULL) {
    size_list_head = new_size_list_entry(size, path_list);
    size_list_tail = size_list_head;
    stats_size_list_count = 1;
    return size_list_head;
  }

  struct size_list * new_entry = new_size_list_entry(size, path_list);
  size_list_tail->next = new_entry;
  size_list_tail = size_list_tail->next;
  stats_size_list_count++;
  return new_entry;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void analyze_process_size_list(sqlite3 * dbh)
{
  if (size_list_head == NULL) {
    return;
  }

  char * line;
  char * node;
  char * path_list_head;
  int count = 0;
  long total_blocks;
  long total_blocks_initial;
  int analyze_block_size = hash_one_block_size;
  off_t skip = 0;

  struct size_list * size_node = size_list_head;

  while (size_node != NULL) {

    if (size_node->size < 0) {
      printf("size makes no sense!\n");                     // LCOV_EXCL_LINE
      exit(1);                                              // LCOV_EXCL_LINE
    }

    total_blocks = 1 + (size_node->size / analyze_block_size);
    total_blocks_initial = total_blocks;
    skip = 0;
    count++;
    path_list_head = size_node->path_list;
    node = pl_get_first_entry(path_list_head);

    LOG_PROGRESS {
      uint32_t path_count = pl_get_path_count(path_list_head);
      LOG(L_PROGRESS, "Processing %d/%d "
          "(%d files of size %ld) (%ld blocks of size %d)\n",
          count, stats_size_list_count, path_count, (long)size_node->size,
          total_blocks, analyze_block_size);
    }

    // Build initial hash list for these files

    int hl_current = 1;
    struct hash_list * hl_one = get_hash_list(hl_current);
    do {
      line = pl_entry_get_path(node);
      add_hash_list(hl_one, line, 1, analyze_block_size, skip);
      node = pl_entry_get_next(node);
    } while (node != NULL);

    LOG_TRACE {
      LOG(L_TRACE, "Contents of hash list hl_one:\n");
      print_hash_list(hl_one);
    }

    struct hash_list * hl_previous = NULL;

    total_blocks--;

    while(1) {

      // If no potential dups after this round, we're done!
      if (HASH_LIST_NO_DUPS(hl_one)) {
        LOG(L_TRACE, "No potential dups left, done!\n");
        stats_sets_dup_not[ROUND1]++;
        goto ANALYZER_CONTINUE;
      }

      // If we've processed all blocks already, we're done!
      if (total_blocks == 0) {
        LOG(L_TRACE, "Some dups confirmed, here they are:\n");
        publish_duplicate_hash_list(dbh, hl_one, size_node->size);
        stats_sets_dup_done[ROUND1]++;
        goto ANALYZER_CONTINUE;
      }

      hl_previous = hl_one;
      hl_current = hl_current == 1 ? 3 : 1;
      hl_one = get_hash_list(hl_current);
      skip++;
      total_blocks--;

      LOG(L_MORE_INFO, "Next round of filtering: skip = %ld\n", (long)skip);
      filter_hash_list(hl_previous, 1, analyze_block_size, hl_one, skip);

      LOG_TRACE {
        LOG(L_TRACE, "Contents of hash list hl_one:\n");
        print_hash_list(hl_one);
      }
    }

  ANALYZER_CONTINUE:

    skip++;
    if (skip == 1) {
      stats_analyzer_one_block++;
    } else if (total_blocks == 0) {
      stats_analyzer_all_blocks++;
    }
                                                             // LCOV_EXCL_START
    if (skip > 1 && total_blocks > 0) {
      int pct = (int)(100 * skip) / total_blocks_initial;
      int bucket = (pct / 5) - 1;
      stats_analyzer_buckets[bucket]++;
    }                                                        // LCOV_EXCL_STOP

    LOG(L_PROGRESS, " Completed after %ld blocks read (%zd remaining)\n",
        (long)skip, total_blocks);

    size_node = size_node->next;
  }
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

    LOG_PROGRESS {
      LOG(L_PROGRESS,
          "Processing %d/%d (%d files of size %ld)\n",
          count, stats_size_list_count, path_count, (long)size_node->size);
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
      LOG(L_INFO, "Computing full hash up front, file is small enough.\n");
    } else {
      stats_one_block_hash_first++;
      round_one_hash_blocks = 1;
    }

    // Build initial hash list for these files
    stats_sets_processed[ROUND1]++;
    struct hash_list * hl_one = get_hash_list(HASH_LIST_ONE);
    do {
      line = pl_entry_get_path(node);
      add_hash_list(hl_one, line, round_one_hash_blocks,
                    hash_one_block_size, 0);
      node = pl_entry_get_next(node);
    } while (node != NULL);

    LOG_TRACE {
      LOG(L_TRACE, "Contents of hash list hl_one:\n");
      print_hash_list(hl_one);
    }

    if (save_uniques) {
      skim_uniques(dbh, hl_one, save_uniques);
    }

    // If no potential dups after this round, we're done!
    if (HASH_LIST_NO_DUPS(hl_one)) {
      LOG(L_TRACE, "No potential dups left, done!\n");
      stats_sets_dup_not[ROUND1]++;
      goto CONTINUE;
    }

    // If by now we already have a full hash, publish and we're done!
    if (round_one_hash_blocks == 0) {
      stats_sets_dup_done[ROUND1]++;
      LOG(L_TRACE, "Some dups confirmed, here they are:\n");
      publish_duplicate_hash_list(dbh, hl_one, size_node->size);
      goto CONTINUE;
    }

    struct hash_list * hl_previous = hl_one;

    // Do filtering pass with intermediate number of blocks, if configured
    if (intermediate_blocks > 1) {
      LOG(L_INFO, "Don't know yet. Filtering to second hash list\n");
      stats_sets_processed[ROUND2]++;
      struct hash_list * hl_partial = get_hash_list(HASH_LIST_PARTIAL);
      hl_previous = hl_partial;
      filter_hash_list(hl_one, intermediate_blocks,
                       hash_block_size, hl_partial, 0);

      LOG_TRACE {
        LOG(L_TRACE, "Contents of hash list hl_partial:\n");
        print_hash_list(hl_partial);
      }

      if (save_uniques) {
        skim_uniques(dbh, hl_partial, save_uniques);
      }

      // If no potential dups after this round, we're done!
      if (HASH_LIST_NO_DUPS(hl_partial)) {
        LOG(L_TRACE, "No potential dups left, done!\n");
        stats_sets_dup_not[ROUND2]++;
        goto CONTINUE;
      }

      // If this size < hashed so far, we're done so publish to db
      if (size_node->size < hash_block_size * intermediate_blocks) {
        stats_sets_dup_done[ROUND2]++;
        LOG(L_TRACE, "Some dups confirmed, here they are:\n");
        publish_duplicate_hash_list(dbh, hl_partial, size_node->size);
        goto CONTINUE;
      }
    }

    // for anything left, do full file hash
    LOG(L_INFO, "Don't know yet. Filtering to full hash list\n");
    stats_sets_processed[ROUND3]++;
    struct hash_list * hl_full = get_hash_list(HASH_LIST_FULL);
    filter_hash_list(hl_previous, 0, hash_block_size,
                     hl_full, intermediate_blocks);
    LOG_TRACE {
      LOG(L_TRACE, "Contents of hash list hl_full:\n");
      print_hash_list(hl_full);
    }

    if (save_uniques) {
      skim_uniques(dbh, hl_full, save_uniques);
    }

    // If no potential dups after this round, we're done!
    if (HASH_LIST_NO_DUPS(hl_full)) {
      LOG(L_TRACE, "No potential dups left, done!\n");
      stats_sets_dup_not[ROUND3]++;
      goto CONTINUE;
    }

    // Still something left, go publish them to db
    LOG(L_TRACE, "Finally some dups confirmed, here they are:\n");
    stats_sets_dup_done[ROUND3]++;
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

    // The path may be null if this particular path within this pathlist
    // has been discarded as a potential duplicate already. If so, skip.
    if (path[0] != 0) {
      buffer = (char *)malloc(size_node->bytes_read);
      received = read_file_bytes(path, buffer, size_node->bytes_read, 0);

      if (received != size_node->bytes_read) {
        LOG(L_PROGRESS, "error: read %ld bytes from [%s] but wanted %ld\n",
            (long)received, path, (long)size_node->bytes_read);
        path[0] = 0;
        pl_decrease_path_count(size_node->path_list, 1);
        free(buffer);

      } else {
        pl_entry_set_buffer(node, buffer);
      }

      LOG_TRACE {
        if (received == size_node->bytes_read) {
          LOG(L_TRACE,
              "read %ld bytes from %s\n", (long)size_node->bytes_read,path);
        }
      }

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
  char * self = "                                        [reader] ";
  struct size_list * size_node;
  int round_one;
  int round_two;
  int loops = 0;
  off_t max_to_read;

  pthread_setspecific(thread_name, self);
  LOG(L_THREADS, "Thread created\n");

  do {
    size_node = size_list_head;
    loops++;
    round_one = 0;
    round_two = 0;
    LOG(L_THREADS, "Starting size list loop #%d\n", loops);

    do {
      d_mutex_lock(&size_node->lock, "reader top");

      LOG(L_MORE_THREADS, "(loop %d) size:%ld state:%s\n",
          loops, (long)size_node->size, state_name(size_node->state));

      switch(size_node->state) {

      case SLS_NEED_BYTES_ROUND_1:
        if (size_node->size <= round1_max_bytes) {
          max_to_read = round1_max_bytes;
        } else {
          max_to_read = hash_one_block_size;
        }
        reader_read_bytes(size_node, max_to_read);
        round_one++;
        size_node->state = SLS_READY_1;
        break;

      case SLS_NEED_BYTES_ROUND_2:
        max_to_read = hash_block_size * intermediate_blocks;
        reader_read_bytes(size_node, max_to_read);
        round_two++;
        size_node->state = SLS_READY_2;
        break;
      }

      d_mutex_unlock(&size_node->lock);
      size_node = size_node->next;

    } while (size_node != NULL && reader_continue);

    LOG(L_THREADS, "Finished loop %d: round_one:%d, round_two:%d\n",
        loops, round_one, round_two);

  } while (reader_continue);

  LOG(L_THREADS, "DONE (%d loops)\n", loops);

  return(NULL);
}


/** ***************************************************************************
 * Reads bytes from disk in readlist order (not by sizelist group)
 * and stores the data in the pathlist buffer for each file.
 *
 * Parameters:
 *    max_bytes_to_read - For files less than this size, read entire file.
 *    min_block_size    - Read at least this many bytes.
 *
 * Return: none
 *
 */
static void process_readlist(int max_bytes_to_read, int min_block_size)
{
  struct size_list * sizelist;
  struct read_list_entry * rlentry;
  ssize_t received;
  off_t max_to_read;
  off_t size;
  uint32_t count;
  int rlpos = 0;
  int new_avg;
  long t1;
  long took;
  char * pathlist_entry;
  char * pathlist_head;
  char * path;
  char * buffer;

  do {

    rlentry = &read_list[rlpos];
    pathlist_head = rlentry->pathlist_head;
    pathlist_entry = rlentry->pathlist_self;
    sizelist = pl_get_szl_entry(pathlist_head);

    if (sizelist == NULL) {                                  // LCOV_EXCL_START
      printf("error: sizelist is null in pathlist!\n");
      exit(1);
    }                                                        // LCOV_EXCL_STOP

    LOG_BASE {
      if (pathlist_head != sizelist->path_list) {            // LCOV_EXCL_START
        printf("error: inconsistent sizelist in pathlist head!\n");
        printf("pathlist head (%p) -> sizelist %p\n", pathlist_head, sizelist);
        printf("sizelist (%p) -> pathlist head %p\n",
               sizelist, sizelist->path_list);
        exit(1);
      }                                                      // LCOV_EXCL_STOP
    }

    d_mutex_lock(&sizelist->lock, "process_readlist");

    switch(sizelist->state) {
    case SLS_NEED_BYTES_ROUND_1: break;
    case SLS_NEED_BYTES_ROUND_2: break;
    case SLS_NEEDS_ROUND_3:
    case SLS_DONE:
      goto PRL_NODE_DONE;
    default:                                                 // LCOV_EXCL_START
      printf("error: process_readlist can't handle state %s\n",
             state_name(sizelist->state));
      exit(1);
    }                                                        // LCOV_EXCL_STOP

    size = sizelist->size;
    if (size < 0) {                                          // LCOV_EXCL_START
      printf("error: size %ld makes no sense\n", (long)size);
      exit(1);
    }                                                        // LCOV_EXCL_STOP

    if (size <= max_bytes_to_read) {
      max_to_read = size;
    } else {
      max_to_read = min_block_size;
    }

    if (max_to_read > 0) {

      count = pl_get_path_count(pathlist_head);
      path = pl_entry_get_path(pathlist_entry);
      if (path == NULL || path[0] == 0) {
        goto PRL_NODE_DONE;
      }

      LOG(L_MORE_THREADS, "Set (%d files of size %ld): read %s\n",
          count, (long)size, path);

      buffer = pl_entry_get_buffer(pathlist_entry);
      if (buffer != NULL) {
        free(buffer);
      }

      buffer = (char *)malloc(max_to_read);
      t1 = get_current_time_millis();
      received = read_file_bytes(path, buffer, max_to_read, 0);
      took = get_current_time_millis() - t1;

      if (received != max_to_read) {
        // File may be unreadable or changed size, either way, ignore it.
        LOG(L_PROGRESS, "error: read %ld bytes from [%s] but wanted %ld\n",
            (long)received, path, (long)max_to_read);
        path[0] = 0;
        pl_decrease_path_count(pathlist_head, 1);
        free(buffer);

      } else {

        sizelist->bytes_read = received;
        if (received == size) {
          sizelist->fully_read = 1;
        }

        sizelist->buffers_filled++;
        read_count++;
        new_avg = avg_read_time + (took - avg_read_time) / read_count;
        avg_read_time = new_avg;

        LOG(L_TRACE, " read took %ldms (count=%d avg=%d)\n",
            took, read_count, avg_read_time);

        pl_entry_set_buffer(pathlist_entry, buffer);
      }
    }

  PRL_NODE_DONE:
    d_mutex_unlock(&sizelist->lock);
    rlpos++;

  } while (rlpos < read_list_end);
}


/** ***************************************************************************
 * Reader thread main function for HDD option.
 *
 * This thread reads bytes from disk in readlist order (not by sizelist group)
 * and stores the data in the pathlist buffer for each file.
 *
 * This thread will go through the readlist once (round 1) and then wait
 * until notified. Then, if reader_main_hdd_round_2 is non-zero it will go
 * through the readlist once more (round 2). The thread then exits.
 *
 * Parameters:
 *    arg - Not used.
 *
 * Return: none
 *
 */
static void * reader_main_hdd(void * arg)
{
  (void)arg;
  char * self = "                                    [hdd-reader] ";
  char * self1 = "                                    [hdd-reader1] ";
  char * self2 = "                                    [hdd-reader2] ";

  pthread_setspecific(thread_name, self);
  LOG(L_THREADS, "Thread created\n");

  if (read_list_end == 0) {                                  // LCOV_EXCL_START
    LOG(L_INFO, "readlist is empty, nothing to read\n");
    return NULL;
  }                                                          // LCOV_EXCL_STOP

  pthread_setspecific(thread_name, self1);
  stats_round_start[ROUND1] = get_current_time_millis();
  process_readlist(round1_max_bytes, hash_one_block_size);

  d_mutex_lock(&reader_main_hdd_lock, "reader_main_hdd_lock r1 end");
  stats_round_duration[ROUND1] =
    get_current_time_millis() - stats_round_start[ROUND1];
  d_mutex_unlock(&reader_main_hdd_lock);

  // Having read all round 1 buffers, wait for the main thread to finish
  // processing them.
  LOG(L_THREADS, "Done first loop for round 1, waiting ...\n");

  d_mutex_lock(&reader_main_hdd_lock, "reader_main_hdd_lock");
  d_cond_wait(&reader_main_hdd_cond, &reader_main_hdd_lock);
  d_mutex_unlock(&reader_main_hdd_lock);

  LOG(L_THREADS, "Waking up: round_2:%d\n", reader_main_hdd_round_2);
  stats_round_start[ROUND2] = get_current_time_millis();

  // If any sets are in need of a round 2, go read those in readlist order.
  if (reader_main_hdd_round_2 > 0) {
    pthread_setspecific(thread_name, self2);
    process_readlist(R3_BUFSIZE, 0);
  }

  d_mutex_lock(&reader_main_hdd_lock, "reader_main_hdd_lock r2 end");
  stats_round_duration[ROUND2] =
    get_current_time_millis() - stats_round_start[ROUND2];
  d_mutex_unlock(&reader_main_hdd_lock);

  LOG(L_THREADS, "DONE\n");

  return(NULL);
}


/** ***************************************************************************
 * Helper function to process files from a size node to a hash list.
 * Used for rounds 1 and 2 during hash list processing.
 * The data buffers are freed as each one is consumed.
 *
 * Parameters:
 *    dbh       - db handle for saving duplicates/uniques
 *    size_node - Process this size node (and associated path list.
 *    hl        - Save paths to this hash list.
 *    round     - Current round (for updating stats arrays).
 *
 * Return: 1 if this path list was completed (either confirmed duplicates
 *         or ruled out possibility of being duplicates).
 *
 */
static int build_hash_list_round(sqlite3 * dbh,
                                 struct size_list * size_node,
                                 struct hash_list * hl,
                                 int round)
{
  char * node;
  char * path;
  char * buffer;
  int completed = 0;

  node = pl_get_first_entry(size_node->path_list);

  // For small files, they may have been fully read already
  if (size_node->fully_read) { stats_sets_full_read[round]++; }
  else { stats_sets_part_read[round]++; }

  LOG(L_TRACE, "Building hash list for size %ld (round %d)\n",
      size_node->size, round+1);

  // Build hash list for these files
  do {
    path = pl_entry_get_path(node);

    // The path may be null if this particular path within this pathlist
    // has been discarded as a potential duplicate already. If so, skip.
    if (path[0] != 0) {
      buffer = pl_entry_get_buffer(node);
      add_hash_list_from_mem(hl, path, buffer, size_node->bytes_read);
      free(buffer);
      pl_entry_set_buffer(node, NULL);
    }

    node = pl_entry_get_next(node);
  } while (node != NULL);

  LOG_TRACE {
    LOG(L_TRACE, "Contents of hash list hl:\n");
    print_hash_list(hl);
  }

  // Remove the uniques seen (also save in db if save_uniques)
  int skimmed = skim_uniques(dbh, hl, save_uniques);
  if (skimmed) {
    pl_decrease_path_count(size_node->path_list, skimmed);
  }

  // If no potential dups after this round, we're done!
  if (HASH_LIST_NO_DUPS(hl)) {
    LOG(L_TRACE, "No potential dups left, done!\n");
    stats_sets_dup_not[round]++;
    size_node->state = SLS_DONE;
    completed = 1;

  } else {
    // If by now we already have a full hash, publish and we're done!
    if (size_node->fully_read) {
      stats_sets_dup_done[round]++;
      LOG(L_TRACE, "Some dups confirmed, here they are:\n");
      publish_duplicate_hash_list(dbh, hl, size_node->size);
      size_node->state = SLS_DONE;
      completed = 1;
    }
  }

  size_node->buffers_filled = 0;

  return completed;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void threaded_process_size_list_hdd(sqlite3 * dbh)
{
  struct size_list * size_node;
  struct hash_list * hl;
  pthread_t reader_thread;
  int work_remains;
  int set_completed;
  int loops = 0;
  int in_round_1, out_round_1;
  int in_round_2, out_round_2;
  int in_round_3, out_round_3;
  int seen_done;

  if (size_list_head == NULL) {
    return;
  }

  // Start my companion thread which will read bytes from disk for me
  if (pthread_create(&reader_thread, NULL, reader_main_hdd, NULL)) {
                                                             // LCOV_EXCL_START
    printf("error: unable to create sizelist reader thread!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  // By the time this thread is starting, the size tree is no longer
  // needed, so free it. Might as well do it while my companion thread
  // goes and reads me a few bytes. Could be done elsewhere as well,
  // but it is a convenient time to do it here.
  free_size_tree();

  LOG_THREADS {
    LOG(L_THREADS, "Starting...\n");
    usleep(10000);
  }

  // Then start going through the size list, processing those size entries
  // which have all file data available already.

  do {
    size_node = size_list_head;
    loops++;
    work_remains = 0;
    in_round_1 = 0;
    in_round_2 = 0;
    in_round_3 = 0;
    out_round_1 = 0;
    out_round_2 = 0;
    out_round_3 = 0;
    seen_done = 0;
    char phase;

    LOG(L_THREADS, "Starting size list loop #%d\n", loops);

    do {
      d_mutex_lock(&size_node->lock, "hdd-main top");

      switch(size_node->state) {
      case SLS_NEED_BYTES_ROUND_1: in_round_1++; phase = '1'; break;
      case SLS_NEED_BYTES_ROUND_2: in_round_2++; phase = '2'; break;
      case SLS_NEEDS_ROUND_3:      in_round_3++; goto NODE_DONE;
      case SLS_DONE:               seen_done++;  goto NODE_DONE;

      default:                                               // LCOV_EXCL_START
        printf("error: unexpected state.a %d\n", size_node->state);
        exit(1);
      }                                                      // LCOV_EXCL_STOP

      uint32_t path_count = pl_get_path_count(size_node->path_list);
      int completed = path_count == size_node->buffers_filled;

      LOG(L_MORE_THREADS,
          "Set (%d files of size %ld; %d read) ready?=%d, state: %s\n",
          path_count, (long)size_node->size,
          size_node->buffers_filled, completed,
          state_name(size_node->state));

      if (!completed) {
        work_remains = 1;

      } else {
        // This size set has all the buffers available by now, so do
        // round 1 or 2 on this set.
        LOG(L_MORE_THREADS, "(loop %d) size:%ld state: %s\n",
            loops, (long)size_node->size, state_name(size_node->state));

        switch(size_node->state) {

        case SLS_NEED_BYTES_ROUND_1:
          size_node->state = SLS_READY_1;
          hl = get_hash_list(HASH_LIST_ONE);
          stats_sets_processed[ROUND1]++;

          set_completed = build_hash_list_round(dbh, size_node, hl, ROUND1);
          break;

        case SLS_NEED_BYTES_ROUND_2:
          size_node->state = SLS_READY_2;
          hl = get_hash_list(HASH_LIST_ONE);
          stats_sets_processed[ROUND2]++;

          set_completed = build_hash_list_round(dbh, size_node, hl, ROUND2);
          break;

        default:                                             // LCOV_EXCL_START
          printf("error: impossible state %s\n", state_name(size_node->state));
          exit(1);
        }                                                    // LCOV_EXCL_STOP

        if (!set_completed) {
          // If the file is not huge (using R3_BUFSIZE as cutoff) then
          // let's do a round 2 on it which will be read in sorted order.
          if (size_node->state == SLS_READY_1 &&
              size_node->size <= R3_BUFSIZE) {
            size_node->state = SLS_NEED_BYTES_ROUND_2;
          } else {
            size_node->state = SLS_NEEDS_ROUND_3;
          }

        } else {
          show_processed(stats_size_list_count,
                         path_count, size_node->size, loops, phase);
        }

      }

    NODE_DONE:

      switch(size_node->state) {
      case SLS_NEED_BYTES_ROUND_1: out_round_1++; break;
      case SLS_NEED_BYTES_ROUND_2: out_round_2++; break;
      case SLS_NEEDS_ROUND_3:      out_round_3++; break;
      case SLS_DONE: break;

      default:                                               // LCOV_EXCL_START
        printf("error: unexpected state.b %d\n", size_node->state);
        exit(1);
      }                                                      // LCOV_EXCL_STOP

      d_mutex_unlock(&size_node->lock);
      size_node = size_node->next;

    } while (size_node != NULL);

    LOG(L_THREADS, "Finished size list loop #%d: "
        "R1:%d->%d R2:%d->%d R3:%d->%d (avg_read_time:%d) seen_done:%d\n",
        loops, in_round_1, out_round_1, in_round_2, out_round_2,
        in_round_3, out_round_3, avg_read_time, seen_done);

    // If there are no sets left which need round 1 processing, wake the
    // reader thread up. If there are sets wanting round 2 processing,
    // the reader thread will do so, otherwise it'll just end.
    if (out_round_1 == 0) {
      if (out_round_2 > 0) { reader_main_hdd_round_2 = out_round_2; }
      d_mutex_lock(&reader_main_hdd_lock, "hdd-main waking reader");
      d_cond_signal(&reader_main_hdd_cond);
      d_mutex_unlock(&reader_main_hdd_lock);
    }

    if (out_round_1 > 0 || out_round_2 > 0) {
      work_remains = 1;
    }

    if (work_remains) { usleep((1 + avg_read_time) * 20 * 1000); }
  } while (work_remains);

  LOG(L_THREADS, "DONE (%d loops)\n", loops);

  reader_continue = 0;
  d_join(reader_thread, NULL);

  LOG(L_THREADS, "Joined reader thread\n");

  // Only entries remaining in the size_list are those marked SLS_NEEDS_ROUND_3
  // These will need to be processed in this thread directly.
  process_round_3(dbh);

  show_processed_done(stats_size_list_count);

  LOG(L_THREADS, "DONE\n");
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void threaded_process_size_list(sqlite3 * dbh)
{
  struct size_list * size_node;
  struct hash_list * hl;
  pthread_t reader_thread;
  int work_remains;
  int did_one;
  int loops = 0;
  int round = 0;
  uint32_t path_count = 0;

  if (size_list_head == NULL) {
    return;
  }

  stats_round_start[ROUND1] = get_current_time_millis();

  // Start my companion thread which will read bytes from disk for me
  if (pthread_create(&reader_thread, NULL, reader_main, NULL)) {
                                                             // LCOV_EXCL_START
    printf("error: unable to create sizelist reader thread!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  LOG_THREADS {
    LOG(L_THREADS, "Starting...\n");
    usleep(10000);
  }

  // By the time this thread is starting, the size tree is no longer
  // needed, so free it. Might as well do it while my companion thread
  // goes and reads me a few bytes. Could be done elsewhere as well,
  // but it is a convenient time to do it here.
  free_size_tree();

  // Then start going through the size list, processing size entries which
  // have data available.

  do {
    size_node = size_list_head;
    loops++;
    work_remains = 0;

    LOG(L_THREADS, "Starting size list loop #%d\n", loops);

    do {
      did_one = 0;
      d_mutex_lock(&size_node->lock, "main top");

      LOG(L_MORE_THREADS, "(loop %d) size:%ld state:%s\n",
          loops, (long)size_node->size, state_name(size_node->state));

      switch(size_node->state) {

      case SLS_NEED_BYTES_ROUND_1:
      case SLS_NEED_BYTES_ROUND_2:
        work_remains = 1;
        break;

      case SLS_READY_1:
        hl = get_hash_list(HASH_LIST_ONE);
        stats_sets_processed[ROUND1]++;

        did_one = build_hash_list_round(dbh, size_node, hl, ROUND1);
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
        stats_sets_processed[ROUND2]++;

        did_one = build_hash_list_round(dbh, size_node, hl, ROUND2);
        round = 2;
        if (!did_one) {
          size_node->state = SLS_NEEDS_ROUND_3;
        }
        break;
      }

      if (did_one) {
        LOG_PROGRESS {
          path_count = pl_get_path_count(size_node->path_list);
        }
        show_processed(stats_size_list_count, path_count,
                       size_node->size, round, '1');
      }

      d_mutex_unlock(&size_node->lock);
      size_node = size_node->next;

    } while (size_node != NULL);

  } while (work_remains);

  LOG(L_THREADS, "DONE (%d loops)\n", loops);

  reader_continue = 0;
  d_join(reader_thread, NULL);

  LOG(L_THREADS, "Joined reader thread\n");

  stats_round_duration[ROUND1] =
    get_current_time_millis() - stats_round_start[ROUND1];
  stats_round_duration[ROUND2] = 0;

  // Only entries remaining in the size_list are those marked SLS_NEEDS_ROUND_3
  // These will need to be processed in this thread directly.
  process_round_3(dbh);

  show_processed_done(stats_size_list_count);

  LOG(L_THREADS, "DONE\n");
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void init_size_list()
{
  size_list_head = NULL;
  size_list_tail = NULL;
  deleted_list_head = NULL;
  deleted_list_tail = NULL;
  stats_size_list_count = 0;
  stats_size_list_avg = 0;
  round1_max_bytes = hash_one_block_size * hash_one_max_blocks;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void free_size_list()
{
  if (size_list_head != NULL) {
    struct size_list * p = size_list_head;
    struct size_list * me = size_list_head;

    while (p != NULL) {
      p = p->next;
      pthread_mutex_destroy(&me->lock);
      free(me);
      me = p;
    }
  }

  if (deleted_list_head != NULL) {
    struct size_list * p = deleted_list_head;
    struct size_list * me = deleted_list_head;

    while (p != NULL) {
      p = p->dnext;
      pthread_mutex_destroy(&me->lock);
      free(me);
      me = p;
    }
  }
}
