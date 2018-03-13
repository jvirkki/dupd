/*
  Copyright 2012-2018 Jyri J. Virkki <jyri@virkki.com>

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

#include "dirtree.h"
#include "filecompare.h"
#include "hash.h"
#include "hashers.h"
#include "hashlist.h"
#include "main.h"
#include "readlist.h"
#include "scan.h"
#include "sizelist.h"
#include "sizetree.h"
#include "stats.h"
#include "utils.h"

static int round1_max_bytes;
static struct size_list * size_list_head;
static struct size_list * size_list_tail;
static struct size_list * deleted_list_head;
static struct size_list * deleted_list_tail;
static int avg_read_time = 0;
static int read_count = 0;
static int open_files = 0;
static int round2_info_buffers_used = 0;
static int r2_hasher_done = 0;

static pthread_mutex_t show_processed_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t r2_loop_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t r2_loop_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t deleted_list_lock = PTHREAD_MUTEX_INITIALIZER;

#define R2_BUFSIZE (1024 * 256)
#define R2_MAX_SETS 4
#define R2_MAX_OPEN_FILES 4

struct round2_info {
  int state;
  int fd;
  off_t read_from;
  size_t read_count;
  void * hash_ctx;
  char hash_result[HASH_MAX_BUFSIZE];
  int bufsize;
  char * buffer;
};

#define HASHER_THREADS 2


/** ***************************************************************************
 * For debug output, return name of state constants.
 *
 */
static char * inner_state_name(int state)
{
  switch(state) {
    /* TODO remove
  case SLS_NEED_BYTES_ROUND_1: return "SLS_NEED_BYTES_ROUND_1";
  case SLS_READY_1: return "SLS_READY_1";
  case SLS_NEED_BYTES_ROUND_2: return "SLS_NEED_BYTES_ROUND_2";
  case SLS_READY_2: return "SLS_READY_2";
  case SLS_NEEDS_ROUND_3: return "SLS_NEEDS_ROUND_3";
  case SLS_DONE: return "SLS_DONE";
    */
  case SLS_R2_HASH_ME: return "SLS_R2_HASH_ME";
  case SLS_R2_HASH_ME_FINAL: return "SLS_R2_HASH_ME_FINAL";
  case SLS_R2_HASH_DONE: return "SLS_R2_HASH_DONE";
  case SLS_R2_READ_MORE: return "SLS_R2_READ_MORE";
  case SLS_R2_READ_FINAL: return "SLS_R2_READ_FINAL";
  case SLS_R2_HASHER_IGNORE: return "SLS_R2_HASHER_IGNORE";
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
 * Public function, see header file.
 *
 */
void show_processed(int total, int files, long size)
{
  d_mutex_lock(&show_processed_lock, "show_processed");

  stats_size_list_done++;

  LOG(L_PROGRESS, "Processed %d/%d (%d files of size %ld)\n",
      stats_size_list_done, total, files, size);

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
 * The caller MUST hold lock on 'entry'. The lock is RELEASED here.
 *
 */
static void unlink_size_list_entry(struct size_list * entry,
                                   struct size_list * previous)
{
  // For simplicity, don't delete very last size list entry.
  if (entry->next == NULL) {
    d_mutex_unlock(&entry->lock);
    return;
  }

  d_mutex_lock(&entry->next->lock, "unlink entry, lock next");

  if (pthread_mutex_trylock(&previous->lock)) {
    LOG(L_THREADS, "While removing entry size %ld, unable to lock previous, "
        "giving up\n", entry->size);
    d_mutex_unlock(&entry->next->lock);
    d_mutex_unlock(&entry->lock);
    return;
  }

  // Now have lock on previous, entry, next

  if (entry->next->state == SLS_DELETED) {
    LOG(L_THREADS, "While removing entry (size %ld), next node (size %ld) "
        "is SLS_DELETED, giving up\n", entry->size, entry->next->size);
    d_mutex_unlock(&previous->lock);
    d_mutex_unlock(&entry->next->lock);
    d_mutex_unlock(&entry->lock);
    return;
  }

  if (previous->next != entry) {
    LOG(L_THREADS, "While removing entry (size %ld), previous (size %ld) "
        "does not point to me, giving up\n", entry->size, previous->size);
    d_mutex_unlock(&previous->lock);
    d_mutex_unlock(&entry->next->lock);
    d_mutex_unlock(&entry->lock);
    return;
  }

  LOG(L_MORE_THREADS, "Removed size list entry of size %ld. "
      "Entry size=%ld now has next entry size=%ld\n",
      (long)entry->size, (long)previous->size, (long)entry->next->size);

  previous->next = entry->next;

  d_mutex_unlock(&previous->lock);
  d_mutex_unlock(&entry->next->lock);

  entry->state = SLS_DELETED;
  entry->next = NULL;
  entry->dnext = NULL;

  d_mutex_unlock(&entry->lock);

  // The reason entry is added to a deleted list instead of just freed here
  // is that the other thread might be waiting on entry->lock right about now
  // so we can't just destroy the mutex here. Instead, let it be and point
  // entry->next to null so it doesn't continue walking that way.

  d_mutex_lock(&deleted_list_lock, "deleted list");
  if (deleted_list_tail == NULL) {
    deleted_list_head = entry;
    deleted_list_tail = entry;
  } else {
    deleted_list_tail->dnext = entry;
    deleted_list_tail = entry;
  }
  d_mutex_unlock(&deleted_list_lock);
}


/** ***************************************************************************
 * Returns a struct for round2_info OR NULL if none available right now.
 *
 * Parameters: none
 *
 * Return: round2_info buffer or NULL.
 *
 */
static inline struct round2_info * allocate_round2_info()
{
  if (round2_info_buffers_used <= R2_MAX_OPEN_FILES) {
    round2_info_buffers_used++;
    struct round2_info * p =
      (struct round2_info *)malloc(sizeof(struct round2_info));
    p->buffer = (char *)malloc(R2_BUFSIZE);
    return p;
  }

  return NULL;
}


/** ***************************************************************************
 * Release a round2_info buffer. Does NOT free the outer struct.
 *
 * Parameters:
 *    info - Release the buffer from this struct
 *
 * Return: None.
 *
 */
static inline void release_round2_info_buffer(struct round2_info * info)
{
  round2_info_buffers_used--;
  free(info->buffer);
  info->buffer = NULL;
}


/** ***************************************************************************
 * Release a round2_info buffer.
 *
 * Parameters:
 *    info - Free this round2_info struct.
 *
 * Return: None.
 *
 */
static inline void free_round2_info(struct round2_info * info)
{
  free(info);
}


/** ***************************************************************************
 * Hashes incoming file data during round 3.
 * Round 2 covers large files which have not been discarded yet, so these
 * are read in blocks by the reader thread and hashed here.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
static void * round2_hasher(void * arg)
{
  sqlite3 * dbh = (sqlite3 *)arg;
  struct size_list * size_node;
  struct size_list * size_node_next;
  struct round2_info * status;
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
  struct path_list_entry * entry;
  char * path;
  int set_count;
  char * self = "                                        [r2-hasher] ";

  pthread_setspecific(thread_name, self);
  LOG(L_THREADS, "thread created\n");

  struct hash_table * ht = init_hash_table();

  do {
    size_node = size_list_head;
    loops++;
    done = 1;
    sets = R2_MAX_SETS;
    set_count = 1;
    loop_buf_init = 0;
    loop_partial_hash = 0;
    loop_hash_completed = 0;
    loop_set_processed = 0;

    do {

      d_mutex_lock(&size_node->lock, "r2-hasher top");

      // SLS_DELETED means we managed to grab a node that has been moved
      // to the deleted list, so there's nothing to do with it.
      if (size_node->state == SLS_DELETED) {
        done = 0;
        goto R2H_NEXT_NODE;
      }

      int count = size_node->path_list->list_size;
      if ( (opt_compare_two && count == 2) ||
           (opt_compare_three && count == 3) ) {
        goto R2H_NEXT_NODE;
      }

      status = NULL;
      off_t size = size_node->size;

      LOG_THREADS {
        int count = size_node->path_list->list_size;
        LOG(L_THREADS,
            "SET %d (%d files of size %ld) (loop %d) state: %s\n",
            set_count++, count, (long)size, loops,
            pls_state(size_node->path_list->state));
      }

      switch(size_node->path_list->state) {

      case PLS_DONE:
        break;

      case PLS_R2_NEEDED:
        done = 0;
        every_hash_computed = 1;
        entry = pb_get_first_entry(size_node->path_list);

        while (entry != NULL) {
          entry_changed = 0;
          status = (struct round2_info *)entry->buffer;
          path = pb_get_filename(entry);

          LOG_MORE_THREADS {
            char buffer[DUPD_PATH_MAX];
            build_path(entry, buffer);
            if (status == NULL) {
              LOG(L_MORE_THREADS, "   entry state: NULL [%s]\n", buffer);
            } else {
              LOG(L_MORE_THREADS, "   entry state: %s [%s]\n",
                  inner_state_name(status->state), buffer);
            }
          }

          if (status == NULL) {

            // If path is empty this is a discarded entry
            if (path[0] != 0) {
              every_hash_computed = 0;
              status = allocate_round2_info();
              if (status == NULL) {
                sets = 0;
                goto R2H_NEXT_NODE;
              }
              loop_buf_init++;
              entry->buffer = (char *)status;
              status->read_from = 0;
              if (size < R2_BUFSIZE) {
                status->read_count = size;
                status->state = SLS_R2_READ_FINAL;
              } else {
                status->read_count = R2_BUFSIZE;
                status->state = SLS_R2_READ_MORE;
              }
              status->bufsize = status->read_count;
              status->hash_ctx = hash_fn_buf_init();
              entry_changed = 1;
            }

          } else {

            switch (status->state) {

            case SLS_R2_HASH_ME:
              every_hash_computed = 0;
              hash_fn_buf_update(status->hash_ctx,
                                 status->buffer, status->bufsize);
              status->read_from = status->read_from + status->bufsize;
              status->read_count = size - status->read_from;
              if (status->read_count > R2_BUFSIZE) {
                status->read_count = R2_BUFSIZE;
                status->state = SLS_R2_READ_MORE;
              } else {
                status->state = SLS_R2_READ_FINAL;
              }
              status->bufsize = status->read_count;
              entry_changed = 1;
              loop_partial_hash++;
              break;

            case SLS_R2_HASH_ME_FINAL:
              hash_fn_buf_final(status->hash_ctx, status->buffer,
                                status->bufsize, status->hash_result);
              status->state = SLS_R2_HASH_DONE;
              status->hash_ctx = NULL; // must have been free'd by buf_final
              release_round2_info_buffer(status);
              entry_changed = 1;
              loop_hash_completed++;
              break;

            case SLS_R2_READ_MORE:
            case SLS_R2_READ_FINAL:
              every_hash_computed = 0;
              break;

            case SLS_R2_HASH_DONE:
              break;

            default:                                         // LCOV_EXCL_START
              printf("error: impossible inner state %s in round2_hasher!\n",
                     inner_state_name(status->state));
              exit(1);
            }                                                // LCOV_EXCL_STOP
          }

          if (entry_changed) {
            LOG_MORE_THREADS {
              char buffer[DUPD_PATH_MAX];
              build_path(entry, buffer);
              LOG(L_MORE_THREADS, "          => : %s [%s]\n",
                  inner_state_name(status->state), buffer);
            }
          }

          entry = entry->next;
        }

        if (every_hash_computed) {

          loop_set_processed++;
          stats_sets_processed[ROUND2]++;
          entry = pb_get_first_entry(size_node->path_list);
          reset_hash_table(ht);

          do {
            path = pb_get_filename(entry);

            // The path may be null if this particular path within this
            // pathlist has been discarded as a potential duplicate already.
            // If so, skip.
            if (path[0] != 0) {
              status = (struct round2_info *)entry->buffer;
              add_to_hash_table(ht, entry, status->hash_result);
              free_round2_info(status);
              entry->buffer = NULL;
            }
            entry = entry->next;
          } while (entry != NULL);

          LOG_TRACE {
            LOG(L_TRACE, "Contents of hash list ht:\n");
            print_hash_table(ht);
          }

          if (save_uniques) {
            skim_uniques(dbh, ht, save_uniques);
          }

          // If no potential dups after this round, we're done!
          if (!hash_table_has_dups(ht)) {

            LOG_TRACE {
              LOG(L_TRACE, "No potential dups left, done!\n");
              LOG(L_TRACE, "Discarded in round 2 the potentials: ");
              entry = pb_get_first_entry(size_node->path_list);
              do {
                path = pb_get_filename(entry);
                if (path[0] != 0) {
                  char buffer[DUPD_PATH_MAX];
                  build_path(entry, buffer);
                  LOG(L_TRACE, "%s ", buffer);
                }
                entry = entry->next;
              } while (entry != NULL);
              LOG(L_TRACE, "\n");
            }

            stats_sets_dup_not[ROUND2]++;
            size_node->path_list->state = PLS_DONE;

          } else {
            // Still something left, go publish them to db
            LOG(L_TRACE, "Finally some dups confirmed, here they are:\n");
            stats_sets_dup_done[ROUND2]++;
            publish_duplicate_hash_table(dbh, ht, size_node->size, ROUND2);
            size_node->path_list->state = PLS_DONE;
          }

          LOG_PROGRESS {
            path_count = size_node->path_list->list_size;
          }

          show_processed(stats_size_list_count, path_count, size_node->size);

        } // every_hash_computed

        break;

      default:                                               // LCOV_EXCL_START
        printf("error: impossible state %s in round2_hasher!\n",
               pls_state(size_node->path_list->state));
        exit(1);
      }                                                      // LCOV_EXCL_STOP

    R2H_NEXT_NODE:
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
      d_mutex_lock(&r2_loop_lock, "r2-hasher loop end no work");
      d_cond_signal(&r2_loop_cond);
      LOG(L_THREADS, "Waiting for something to do...\n");
      d_cond_wait(&r2_loop_cond, &r2_loop_lock);
      d_mutex_unlock(&r2_loop_lock);
    } else {
      d_mutex_lock(&r2_loop_lock, "r2-hasher loop end work done");
      d_cond_signal(&r2_loop_cond);
      d_mutex_unlock(&r2_loop_lock);
    }

  } while (!done);

  r2_hasher_done = 1;
  d_mutex_lock(&r2_loop_lock, "r2-hasher all done");
  d_cond_signal(&r2_loop_cond);
  d_mutex_unlock(&r2_loop_lock);

  LOG(L_THREADS, "DONE (%d loops)\n", loops);
  stats_hasher_loops[ROUND2][0] = loops;

  free_hash_table(ht);

  return NULL;
}


/** ***************************************************************************
 * Read from a file which might be open already, for round 2.
 *
 * Parameters:
 *    entry  - path entry being processed
 *    status - status of this file
 *
 * Return: bytes read
 *
 */
static inline ssize_t round2_reader(struct path_list_entry * entry,
                                    struct round2_info * status)
{
  // If this is the first read request, file isn't open yet
  if (status->read_from == 0) {

    char path[DUPD_PATH_MAX];
    build_path(entry, path);

    status->fd = open(path, O_RDONLY);
    if (status->fd < 0) {                                    // LCOV_EXCL_START
      printf("Error opening [%s]\n", path);
      perror(NULL);
      exit(1);
    }                                                        // LCOV_EXCL_STOP

    // We will be reading the entire file in round2
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
 * Process entries in size list for round2.
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
static void process_round_2(sqlite3 * dbh)
{
  struct size_list * size_node;
  struct size_list * previous_size_node;
  struct size_list * next_node;
  struct size_list * next_node_next;
  struct round2_info * status;
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
  int remaining;

  struct path_list_entry * node;
  char * path = NULL;

  stats_round_start[ROUND2] = get_current_time_millis();

  // Purge sets which are already done by skipping over them so
  // we don't need to look at them again.
  skipped = 0;
  remaining = 0;
  size_node = size_list_head;
  while (size_node != NULL) {
    next_node = size_node->next;
    while (next_node != NULL && next_node->path_list->state == PLS_DONE) {
      next_node_next = next_node->next;
      pthread_mutex_destroy(&next_node->lock);
      free(next_node);
      skipped++;
      next_node = next_node_next;
    }
    if (size_node->path_list->state != PLS_DONE) { remaining++; }
    if (size_node->path_list->state == PLS_NEW) {
        printf("error: path list in PLS_NEW state!\n");
        dump_path_list("bad state", size_node->size, size_node->path_list);
    }
    size_node->next = next_node;
    size_node = size_node->next;
  }

  LOG(L_INFO, "Purged %d size list entries in DONE state\n", skipped);

  LOG(L_INFO, "Entering round2, size list entries remaining: %d\n", remaining);
  if (remaining == 0) {
    goto R2_DONE;
  }

  // Start my companion hasher thread
  if (pthread_create(&hasher_thread, NULL, round2_hasher, dbh)) {
                                                             // LCOV_EXCL_START
    printf("error: unable to create round2_hasher thread!\n");
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
      d_mutex_lock(&size_node->lock, "r2-reader top");
      path_count = size_node->path_list->list_size;

      LOG_THREADS {
        off_t size = size_node->size;
        LOG(L_THREADS,
            "SET %d (%d files of size %ld) (loop %d) state: %s\n",
            set_count++, path_count, (long)size, loops,
            pls_state(size_node->path_list->state));
      }

      switch(size_node->path_list->state) {

      case PLS_R2_NEEDED:
        done = 0;
        node = pb_get_first_entry(size_node->path_list);

        // If we only have two files of this size, compare them directly
        if (opt_compare_two && path_count == 2) {
          char path1[DUPD_PATH_MAX];
          char path2[DUPD_PATH_MAX];
          struct path_list_entry * node1= pl_entry_get_valid_node(node);
          struct path_list_entry * node2= pl_entry_get_valid_node(node1->next);
          build_path(node1, path1);
          build_path(node2, path2);
          compare_two_files(dbh, path1, path2, size_node->size, ROUND2);
          stats_two_file_compare++;
          size_node->path_list->state = PLS_DONE;
          did_one = 1;
          done_something = 1;
          read_two++;
          break;
        }

        // If we only have three files of this size, compare them directly
        if (opt_compare_three && path_count == 3) {
          char path1[DUPD_PATH_MAX];
          char path2[DUPD_PATH_MAX];
          char path3[DUPD_PATH_MAX];
          struct path_list_entry * node1= pl_entry_get_valid_node(node);
          struct path_list_entry * node2= pl_entry_get_valid_node(node1->next);
          struct path_list_entry * node3= pl_entry_get_valid_node(node2->next);
          build_path(node1, path1);
          build_path(node2, path2);
          build_path(node3, path3);
          compare_three_files(dbh, path1, path2, path3,size_node->size,ROUND2);
          stats_three_file_compare++;
          size_node->path_list->state = PLS_DONE;
          did_one = 1;
          done_something = 1;
          read_three++;
          break;
        }

        while (node != NULL) {
          status = (struct round2_info *)node->buffer;
          changed = 0;
          skipped = 0;
          path = pb_get_filename(node);

          LOG_MORE_THREADS {
            char buffer[DUPD_PATH_MAX];
            build_path(node, buffer);
            if (status == NULL) {
              LOG(L_MORE_THREADS, "   entry state: NULL [%s]\n", buffer);
            } else {
              LOG(L_MORE_THREADS, "   entry state: %s @%ld [%s]\n",
                  inner_state_name(status->state),
                  (long)status->read_from, buffer);
            }
          }

          // While walking the list, when we reach an entry with no status
          // (and valid path), it means we're past the max number of read
          // buffers, so might as well skip to the end of the list.
          if (status == NULL && path != NULL && path[0] != 0) {
            d_mutex_unlock(&size_node->lock);
            size_node = NULL;
            goto PR2_NEXT_NODE;
          }

          if (status != NULL) {

            switch (status->state) {

            case SLS_R2_READ_MORE:
              round2_reader(node, status);
              status->state = SLS_R2_HASH_ME;
              changed = 1;
              done_something = 1;
              read_partial++;
              break;

            case SLS_R2_READ_FINAL:
              round2_reader(node, status);
              close(status->fd);
              open_files--;
              status->fd = 0;
              status->state = SLS_R2_HASH_ME_FINAL;
              changed = 1;
              done_something = 1;
              read_final++;
              break;

            case SLS_R2_HASH_ME:
            case SLS_R2_HASH_ME_FINAL:
            case SLS_R2_HASH_DONE:
              break;

            default:                                         // LCOV_EXCL_START
              printf("error: impossible inner state %d in process_round2!\n",
                     status->state);
              exit(1);
            }                                                // LCOV_EXCL_STOP
          }

          if (changed) {
            LOG_MORE_THREADS {
              char buffer[DUPD_PATH_MAX];
              build_path(node, buffer);
              LOG(L_MORE_THREADS, "          => : %s [%s]\n",
                inner_state_name(status->state), buffer);
            }
          }

          if (skipped) {
            node = NULL;
          } else {
            node = node->next;
          }

        }

        break;

      case PLS_DONE:
        if (previous_size_node != NULL) {
          free_myself = 1;
        }
        break;

      default:                                               // LCOV_EXCL_START
        printf("In final pass, bad sizelist state %d (%s)\n",
               size_node->path_list->state,
               pls_state(size_node->path_list->state));
        exit(1);
      }                                                      // LCOV_EXCL_STOP

      if (did_one) {
        LOG_PROGRESS {
          path_count = size_node->path_list->list_size;
        }
        show_processed(stats_size_list_count, path_count, size_node->size);
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

    PR2_NEXT_NODE:
      LOG(L_MORE_TRACE, "end of path loop");

    } while (size_node != NULL);

    LOG(L_THREADS, "Finished size list loop #%d: R2:%d R3:%d Rp:%d Rf:%d\n",
        loops, read_two, read_three, read_partial, read_final);

    if (only_testing) {
      slow_down(10, 100);
    }

    // Let the hasher thread know I did something.
    if (done_something) {
      d_mutex_lock(&r2_loop_lock, "r2-reader loop end work done");
      d_cond_signal(&r2_loop_cond);
      d_mutex_unlock(&r2_loop_lock);
    } else {
      if (!r2_hasher_done) {
        d_mutex_lock(&r2_loop_lock, "r2-reader loop end no work");
        d_cond_signal(&r2_loop_cond);
        LOG(L_THREADS, "Waiting for something to do...\n");
        d_cond_wait(&r2_loop_cond, &r2_loop_lock);
        d_mutex_unlock(&r2_loop_lock);
      }
    }

  } while (!done);

  if (!r2_hasher_done) {
    LOG(L_THREADS, "Waiting for hasher thread...\n");
    d_mutex_lock(&r2_loop_lock, "r2-reader all done");
    d_cond_signal(&r2_loop_cond);
    d_mutex_unlock(&r2_loop_lock);
  }

  d_join(hasher_thread, NULL);

 R2_DONE:
  d_mutex_lock(&status_lock, "r2-reader end");
  stats_round_duration[ROUND2] =
    get_current_time_millis() - stats_round_start[ROUND2];
  pthread_cond_signal(&status_cond);
  d_mutex_unlock(&status_lock);

  stats_reader_loops[ROUND2] = loops;
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
static struct size_list * new_size_list_entry(off_t size,
                                              struct path_list_head *path_list)
{
  struct size_list * e = (struct size_list *)malloc(sizeof(struct size_list));
  e->size = size;
  e->path_list = path_list;
  //  e->state = SLS_NEED_BYTES_ROUND_1;
  e->state = 0;
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
 * Read bytes from disk for the reader thread.
 *
 * Bytes are read to a buffer allocated for each path node. If a prior buffer
 * is present, free it first.
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
  struct path_list_entry * node;
  char path[DUPD_PATH_MAX];
  char * buffer;
  ssize_t received;

  node = pb_get_first_entry(size_node->path_list);

  if (size_node->size <= max_to_read) {
    size_node->bytes_read = size_node->size;
    size_node->fully_read = 1;
  } else {
    size_node->bytes_read = max_to_read;
    size_node->fully_read = 0;
  }

  do {
    build_path(node, path);

    // The path may be null if this particular path within this pathlist
    // has been discarded as a potential duplicate already. If so, skip.
    if (path[0] != 0) { // TODO
      buffer = (char *)malloc(size_node->bytes_read);
      received = read_file_bytes(path, buffer, size_node->bytes_read, 0);

      if (received != size_node->bytes_read) {
        LOG(L_PROGRESS, "error: read %ld bytes from [%s] but wanted %ld\n",
            (long)received, path, (long)size_node->bytes_read);
        { // TODO
          char * fname = pb_get_filename(node);
          fname[0] = 0;
        }
        size_node->path_list->list_size--;
        free(buffer);

      } else {
        node->buffer = buffer;
        node->state = FS_R1_BUFFER_FILLED;
      }

      LOG_TRACE {
        if (received == size_node->bytes_read) {
          LOG(L_TRACE,
              "read %ld bytes from %s\n", (long)size_node->bytes_read,path);
        }
      }

    } else {
      node->buffer = NULL;
    }

    node = node->next;

  } while (node != NULL);
}


/** ***************************************************************************
 * Tell hasher threads that reader is done. Used by SSD and HDD reader threads.
 *
 * Parameters:
 *    hasher_info - Array of hasher thread params.
 *
 * Return: none
 *
 */
static void signal_hashers(struct hasher_param * hasher_info)
{
  struct hasher_param * queue_info = NULL;

  // Let hashers know I'm done
  for (int n = 0; n < HASHER_THREADS; n++) {
    queue_info = &hasher_info[n];
    d_mutex_lock(&queue_info->queue_lock, "reader saying bye");
    queue_info->done = 1;
    LOG(L_MORE_THREADS, "Marking hasher thread %d done\n", n);
    d_cond_signal(&queue_info->queue_cond);
    d_mutex_unlock(&queue_info->queue_lock);
  }
}


/** ***************************************************************************
 * Add a path list to the work queue of a hasher thread.
 *
 * Parameters:
 *    thread        - Add it to this thread's queue.
 *    pathlist_head - Add this pathlist to the queue.
 *    hasher_info   - Array of hasher thread params.
 *
 * Return: none
 *
 */
static void submit_path_list(int thread,
                             struct path_list_head * pathlist_head,
                             struct hasher_param * hasher_info)
{
  struct size_list * sizelist = pathlist_head->sizelist;
  long size = (long)sizelist->size;
  int count = pathlist_head->list_size;
  struct hasher_param * queue_info = NULL;

  LOG(L_THREADS, "Inserting set (%d files of size %ld) in state %s "
      "into hasher queue %d\n",
      count, size, pls_state(pathlist_head->state), thread);

  queue_info = &hasher_info[thread];

  d_mutex_lock(&queue_info->queue_lock, "adding to queue");

  if (queue_info->queue_pos == HASHER_QUEUE_SIZE - 1) {
    // TODO resize
    LOG(L_THREADS, "Hasher queue %d is full (pos=%d), waiting...\n",
        thread, queue_info->queue_pos);
    printf("XXX hasher queue full, having to stall...\n");
    while (queue_info->queue_pos > HASHER_QUEUE_SIZE - 2) {
      d_cond_wait(&queue_info->queue_cond, &queue_info->queue_lock);
    }
  }

  queue_info->queue_pos++;
  queue_info->queue[queue_info->queue_pos] = pathlist_head;
  LOG(L_MORE_THREADS, "Set (%d files of size %ld) now in queue %d pos %d\n",
      count, size, thread, queue_info->queue_pos);

  // Signal the corresponding hasher thread so it'll pick up the pathlist
  d_cond_signal(&queue_info->queue_cond);
  d_mutex_unlock(&queue_info->queue_lock);
}


/** ***************************************************************************
 * Reader thread for HDD mode.
 *
 * This thread reads bytes from disk in readlist order (not by sizelist group)
 * and stores the data in the pathlist buffer for each file.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
static void * read_list_reader(void * arg)
{
  char * self = "                                        [RL-reader] ";
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
  struct path_list_entry * pathlist_entry;
  struct path_list_head * pathlist_head;
  char path[DUPD_PATH_MAX];
  char * buffer;
  struct hasher_param * hasher_info = (struct hasher_param *)arg;
  int next_queue = 0;

  pthread_setspecific(thread_name, self);
  LOG(L_THREADS, "Thread created\n");

  if (read_list_end == 0) {                                  // LCOV_EXCL_START
    LOG(L_INFO, "readlist is empty, nothing to read\n");
    return NULL;
  }                                                          // LCOV_EXCL_STOP

  rlpos = 0;
  LOG(L_THREADS, "Starting HDD read list\n");

  do {

    rlentry = &read_list[rlpos];
    pathlist_head = rlentry->pathlist_head;
    pathlist_entry = rlentry->pathlist_self;
    sizelist = pathlist_head->sizelist;

    // A path list may have been marked done earlier if it was down to one
    // file. Also a path entry may have been marked invalid when removing
    // duplicate hardlinks. In either case, nothing to do for this entry.

    if (pathlist_entry->state == FS_INVALID ||
        pathlist_head->state == PLS_DONE) {
      pathlist_entry->state = FS_INVALID;
      rlpos++;
      continue;
    }

    //    d_mutex_lock(&sizelist->lock, "process_readlist");

    size = sizelist->size;

    if (size <= round1_max_bytes) {
      max_to_read = size;
    } else {
      max_to_read = hash_one_block_size;
    }

    count = pathlist_head->list_size;
    build_path(pathlist_entry, path);

    if (path[0] == 0) {
      printf("error: path zero len\n");
      exit(1);
    }

    LOG(L_MORE_THREADS, "Set (%d files of size %ld in state %s): read %s\n",
        count, (long)size, file_state(pathlist_entry->state), path);

    buffer = (char *)malloc(max_to_read);
    t1 = get_current_time_millis();
    received = read_file_bytes(path, buffer, max_to_read, 0);
    took = get_current_time_millis() - t1;

    if (received != max_to_read) {
      // File may be unreadable or changed size, either way, ignore it.
      LOG(L_PROGRESS, "error: read %ld bytes from [%s] but wanted %ld\n",
          (long)received, path, (long)max_to_read);
      pathlist_entry->state = FS_INVALID;
      free(buffer);
      pathlist_head->list_size--;
      // If this path list is down to 1 entry, there is no work
      // remaining so mark it done.
      if (pathlist_head->list_size <= 1) {
        pathlist_head->state = PLS_DONE;
        stats_sets_dup_not[ROUND1]++;
        LOG(L_MORE_THREADS, "Set (%d files of size %ld): state now %s\n",
            pathlist_head->list_size, (long)size,
            pls_state(pathlist_head->state));
      }

    } else {
      pathlist_entry->buffer = buffer;
      pathlist_entry->state = FS_R1_BUFFER_FILLED;
      sizelist->bytes_read = received;
      if (received == size) {
        sizelist->fully_read = 1;
      }

      sizelist->buffers_filled++;
      if (sizelist->buffers_filled == count) {
        pathlist_head->state = PLS_R1_BUFFERS_FULL;
        submit_path_list(next_queue, pathlist_head, hasher_info);
        next_queue = (next_queue + 1) % HASHER_THREADS;
      }

      read_count++;
      new_avg = avg_read_time + (took - avg_read_time) / read_count;
      avg_read_time = new_avg;

      LOG(L_TRACE, " read took %ldms (count=%d avg=%d)\n",
          took, read_count, avg_read_time);
    }

    //    d_mutex_unlock(&sizelist->lock);
    rlpos++;

  } while (rlpos < read_list_end);

  LOG(L_THREADS, "DONE\n");

  return NULL;
}


/** ***************************************************************************
 * Size list-based reader thread (used in SSD mode only).
 *
 * Loops through the size list, looking for size entries which need
 * bytes read from disk and then reads the needed bytes and saves them
 * in the path list.
 *
 * Parameters:
 *    arg - Not used.
 *
 * Return: none
 *
 */
static void * size_list_reader(void * arg)
{
  char * self = "                                        [SL-reader] ";
  struct size_list * size_node;
  struct size_list * size_node_next;
  int sets;
  int next_queue = 0;
  off_t max_to_read;
  struct hasher_param * hasher_info = (struct hasher_param *)arg;

  pthread_setspecific(thread_name, self);
  LOG(L_THREADS, "Thread created\n");

  size_node = size_list_head;
  sets = 0;

  do {
    d_mutex_lock(&size_node->lock, "reader top");

    LOG(L_MORE_THREADS, "SET %d size:%ld state:%s\n", ++sets,
        (long)size_node->size, pls_state(size_node->path_list->state));

    if (size_node->size <= round1_max_bytes) {
      max_to_read = round1_max_bytes;
    } else {
      max_to_read = hash_one_block_size;
    }

    reader_read_bytes(size_node, max_to_read);
    size_node->path_list->state = PLS_R1_BUFFERS_FULL;
    submit_path_list(next_queue, size_node->path_list, hasher_info);
    next_queue = (next_queue + 1) % HASHER_THREADS;

    size_node_next = size_node->next;
    d_mutex_unlock(&size_node->lock);
    size_node = size_node_next;

  } while (size_node != NULL);

  LOG(L_THREADS, "DONE\n");

  return NULL;
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
struct size_list * add_to_size_list(off_t size,
                                    struct path_list_head * path_list)
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


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void process_size_list(sqlite3 * dbh)
{
  pthread_t reader_thread;
  struct hasher_param hasher_info[HASHER_THREADS];

  if (size_list_head == NULL) {
    return;
  }

  for (int n = 0; n < HASHER_THREADS; n++) {
    hasher_info[n].thread_num = n;
    hasher_info[n].done = 0;
    hasher_info[n].dbh = dbh;
    pthread_mutex_init(&hasher_info[n].queue_lock, NULL);
    pthread_cond_init(&hasher_info[n].queue_cond, NULL);
    hasher_info[n].queue_pos = -1;
    for (int i = 0; i < HASHER_QUEUE_SIZE; i++) {
      hasher_info[n].queue[i] = NULL;
    }
  }

  stats_round_start[ROUND1] = get_current_time_millis();

  // Start appropriate file reader thread
  LOG(L_THREADS, "Starting file reader thread...\n");
  if (hdd_mode) { d_create(&reader_thread, read_list_reader, &hasher_info); }
  else { d_create(&reader_thread, size_list_reader, &hasher_info); }

  // Meanwhile, the size tree is no longer needed, so free it. Might
  // as well do it while this thread has nothing else to do but wait.
  free_size_tree();

  usleep(10000);

  // Start hasher threads and then wait for them to finish...

  LOG(L_THREADS, "Starting %d threads...\n", HASHER_THREADS);
  for (int n = 0; n < HASHER_THREADS; n++) {
    d_create(&hasher_info[n].thread, round1_hasher, &hasher_info[n]);
  }

  LOG(L_THREADS, "process_size_list: waiting for workers to finish\n");

  d_join(reader_thread, NULL);
  LOG(L_THREADS, "process_size_list: joined reader thread\n");
  signal_hashers(hasher_info);

  for (int n = 0; n < HASHER_THREADS; n++) {
    d_join(hasher_info[n].thread, NULL);
    LOG(L_THREADS, "process_size_list: joined hasher thread %d\n", n);
  }

  long now = get_current_time_millis();
  stats_round_duration[ROUND1] = now - stats_round_start[ROUND1];

  // Process any remaining entries in round 2.
  process_round_2(dbh);
}
