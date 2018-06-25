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
#include <errno.h>
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

static struct size_list * size_list_head;
static struct size_list * size_list_tail;
static int avg_read_time = 0;
static int read_count = 0;

static pthread_mutex_t show_processed_lock = PTHREAD_MUTEX_INITIALIZER;

#define HASHER_THREADS 2


/** ***************************************************************************
 * Debug output, show the whole size list.
 *
 */
static void dump_size_list()
{
  struct size_list * node = size_list_head;

  printf("--- DUMP SIZE LIST\n");
  while (node != NULL) {
    printf("size          : %" PRIu64 "\n", node->size);
    printf("fully read    : %d\n", node->fully_read);
    printf("next          : %p\n", node->next);
    printf("  == pathlist follows:\n");
    dump_path_list("  == pathlist follows", node->size, node->path_list, 1);

    node = node->next;
  }
  printf("--- END SIZE LIST\n");
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void show_processed(int total, int files, uint64_t size)
{
  d_mutex_lock(&show_processed_lock, "show_processed");

  stats_size_list_done++;

  LOG(L_PROGRESS, "Processed %d/%d (%d files of size %" PRIu64 ")\n",
      stats_size_list_done, total, files, size);

  if (stats_size_list_done > total) {                        // LCOV_EXCL_START
    printf("\nThat's not right...\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  d_mutex_unlock(&show_processed_lock);
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
static struct size_list * new_size_list_entry(uint64_t size,
                                              struct path_list_head *path_list)
{
  struct size_list * e = (struct size_list *)malloc(sizeof(struct size_list));
  e->size = size;
  e->path_list = path_list;
  e->fully_read = 0;
  if (pthread_mutex_init(&e->lock, NULL)) {
                                                             // LCOV_EXCL_START
    printf("error: new_size_list_entry mutex init failed!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP
  e->next = NULL;
  return e;
}


/** ***************************************************************************
 * Tell hasher threads that reader is done.
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
  uint64_t size = sizelist->size;
  struct hasher_param * queue_info = NULL;

  LOG(L_THREADS, "Inserting set (%d files of size %" PRIu64
      ") pass %d in state %s into hasher queue %d\n",
      pathlist_head->list_size, size, pathlist_head->hash_passes,
      pls_state(pathlist_head->state), thread);

  if (pathlist_head->list_size == 0) {
    LOG(L_THREADS, "SKIP set (%d files of size %" PRIu64 ") in state %s ",
        pathlist_head->list_size, size, pls_state(pathlist_head->state));
    pathlist_head->state = PLS_DONE;
    return;
  }

  if (pathlist_head->state != PLS_ALL_BUFFERS_READY) {
    printf("error: pathlist not in correct state for going into queue\n");
    exit(1);
  }

  queue_info = &hasher_info[thread];

  d_mutex_lock(&queue_info->queue_lock, "adding to queue");

  if (queue_info->queue_pos == queue_info->size - 1) {
    LOG(L_THREADS, "Hasher queue %d is full (pos=%d), increasing...\n",
        thread, queue_info->queue_pos);

    int oldend = queue_info->size;
    int newend = queue_info->size * 2;
    int newsize = sizeof(struct path_list_head *) * newend;
    queue_info->queue =
      (struct path_list_head **)realloc(queue_info->queue, newsize);
    queue_info->size = newend;
    for (int i = oldend; i < newend; i++) {
      queue_info->queue[i] = NULL;
    }
    LOG(L_RESOURCES, "Increased hasher queue(%d) to %d entries\n",
        thread, queue_info->size);
  }

  queue_info->queue_pos++;
  queue_info->queue[queue_info->queue_pos] = pathlist_head;
  LOG(L_MORE_THREADS, "Set (%d files of size %" PRIu64
      ") now in queue %d pos %d\n",
      pathlist_head->list_size, size, thread, queue_info->queue_pos);

  // Signal the corresponding hasher thread so it'll pick up the pathlist
  d_cond_signal(&queue_info->queue_cond);
  d_mutex_unlock(&queue_info->queue_lock);
}


/** ***************************************************************************
 * Size list-based reader used to flush buffer usage down.
 *
 * This is called from read_list_reader if the data buffer usage grows
 * beyond desired limit. Here we'll read in size_list order which allows
 * free'ing buffers much sooner (but can be far slower on HDDs).
 *
 * Parameters:
 *    arg - Contains hasher_info array.
 *
 * Return: none
 *
 */
static void * size_list_flusher(void * arg)
{
  struct size_list * size_node;
  struct size_list * size_node_next;
  struct path_list_entry * entry;
  int sets;
  int bfpct;
  int path_count;
  struct hasher_param * hasher_info = (struct hasher_param *)arg;
  struct hash_table * ht = init_hash_table();

  size_node = size_list_head;
  sets = 0;
  stats_flusher_active = 1;

  do {
    d_mutex_lock(&size_node->lock, "flusher");

    LOG(L_THREADS, "FL.SET %d size:%" PRIu64 " state:%s\n", ++sets,
        size_node->size, pls_state(size_node->path_list->state));

    if (size_node->path_list->state == PLS_NEED_DATA) {

      reset_hash_table(ht);
      entry = pb_get_first_entry(size_node->path_list);

      while (entry != NULL) {

        LOG_MORE_THREADS {
          char buffer[DUPD_PATH_MAX];
          build_path(entry, buffer);
          LOG(L_MORE_THREADS, "FL.FILE %s state %s\n",
              buffer, file_state(entry->state));
        }

        switch (entry->state) {

        case FS_NEED_DATA:
        case FS_BUFFER_READY:
          add_hash_table(ht, entry, 0, 0, 0);
          break;

        default:
          printf("flusher bad state: %s\n", file_state(entry->state));
          exit(1);
        }
        entry = entry->next;
      }

      skim_uniques(hasher_info->dbh, size_node->path_list, ht, save_uniques);
      if (hash_table_has_dups(ht)) {
        publish_duplicate_hash_table(hasher_info->dbh, ht, size_node->size);
        increase_dup_counter(size_node->path_list->list_size);
      }

      size_node->path_list->state = PLS_DONE;
      path_count = size_node->path_list->list_size;
      show_processed(s_stats_size_list_count, path_count, size_node->size);

      bfpct = (int)(100 * stats_read_buffers_allocated / buffer_limit);
      if (bfpct < 85) {
        LOG(L_THREADS, "size_list_flusher returning at %d%%\n", bfpct);
        stats_flusher_active = 0;
        d_mutex_unlock(&size_node->lock);
        return NULL;
      }
    }

    size_node_next = size_node->next;
    d_mutex_unlock(&size_node->lock);
    size_node = size_node_next;

  } while (size_node != NULL);

  stats_flusher_active = 0;
  return NULL;
}


/** ***************************************************************************
 * Fill one block (of hash block size being used) for 'entry' if possible.
 * Reads from the current block (next_read_block) until either the desired
 * amount of data has been read or the block is fully read.
 *
 * Also needs to handle if file has gaps on disk.
 *
 * Incoming state: entry is in FS_NEED_DATA.
 *
 * Outgoing state:
 *     If disk block didn't fill the hash block:
 *       - still in FS_NEED_DATA
 *       - next_read_block updated, next_read_byte updated
 *     If hash block filled or file fully read:
 *       - becomes FS_BUFFER_READY (head possibly PLS_ALL_BUFFERS_READY)
 *       - next_read_block updated, next_read_byte updated
 *     If unable to read expected bytes:
 *       - entry marked invalid (which might make head PLS_DONE)
 *
 * Return: true if current disk block was fully consumed.
 *
 */
static int fill_data_block(struct path_list_head * head,
                           struct path_list_entry * entry,
                           char * path)
{
  int rv = 0;
  uint64_t filesize = head->sizelist->size;

  // If we haven't been here before for this entry (or if we had to
  // free it), we'll need a buffer.

  if (entry->buffer == NULL) {
    entry->bufsize = head->wanted_bufsize;
    entry->buffer = (char *)malloc(entry->bufsize);
    entry->next_buffer_pos = 0;
    inc_stats_read_buffers_allocated(entry->bufsize);
  }

  // Or if the desired bufsize has increased since we allocated, realloc.

  if (entry->bufsize != head->wanted_bufsize) {
    uint32_t inc = head->wanted_bufsize - entry->bufsize;
    entry->bufsize = head->wanted_bufsize;
    entry->buffer = (char *)realloc(entry->buffer, entry->bufsize);
    entry->next_buffer_pos = 0;
    inc_stats_read_buffers_allocated(inc);
  }

  if (entry->buffer == NULL) {
    printf("error: unable to allocate read buffer, sorry!\n");
    exit(1);
  }

  struct block_list_entry * bl = &entry->blocks->entry[entry->next_read_block];
  uint64_t current_file_pos = entry->next_read_byte;
  uint64_t current_disk_block_start = bl->start_pos;
  uint64_t current_disk_block_end = bl->start_pos + bl->len;

  // If there is a gap until next available bytes, fill with zeroes as needed
  if (current_file_pos < current_disk_block_start) {
    uint32_t gap_size = current_disk_block_start - current_file_pos;
    uint32_t buf_space = entry->bufsize - entry->next_buffer_pos;
    uint32_t zeroes = gap_size;
    int filling_buffer = 0;
    if (buf_space < zeroes) {
      zeroes = buf_space;
      filling_buffer = 1;
    }
    LOG(L_TRACE, "Gap before in [%s]: gap_size: %" PRIu32 ", buf_space: %"
        PRIu32 ", zeroes: %" PRIu32 "\n", path, gap_size, buf_space, zeroes);
    memset(entry->buffer + entry->next_buffer_pos, 0, zeroes);
    entry->next_buffer_pos += zeroes;
    entry->next_read_byte += zeroes;
    if (filling_buffer) { return rv; }
    current_file_pos = entry->next_read_byte;
  }

  if (current_file_pos < current_disk_block_start ||
      current_file_pos > current_disk_block_end) {
    printf("error: current_file_pos: %" PRIu64 ", current_disk_block_start: %"
           PRIu64 ", current_disk_block_end: %" PRIu64 "\n",
           current_file_pos, current_disk_block_start, current_disk_block_end);
    dump_path_list("", filesize, head, 1);
    exit(1);
  }

  // How much can we read now? Limited either by how much left to read in
  // current disk block or by how much needed to fill memory buffer.

  uint64_t current_disk_block_available =
    current_disk_block_end - current_file_pos;
  uint32_t buffer_available = entry->bufsize - entry->next_buffer_pos;
  uint32_t want_bytes = buffer_available;
  int filling_buffer = 1;
  int consumed_block = 0;
  if (want_bytes >= current_disk_block_available) {
    want_bytes = current_disk_block_available;
    consumed_block = 1;
  }
  if (want_bytes < buffer_available) {
    filling_buffer = 0;
  }

  LOG(L_TRACE, "fill_data_block: [%s] current_file_pos: %" PRIu64
      ", current_disk_block_start: %" PRIu64
      ", current_disk_block_end: %" PRIu64
      ", current_disk_block_available: %" PRIu64
      ", buffer_available: %" PRIu32 ", want_bytes: %" PRIu32
      ", filling_buffer: %d\n",
      path, current_file_pos, current_disk_block_start, current_disk_block_end,
      current_disk_block_available, buffer_available, want_bytes,
      filling_buffer);

  uint64_t bytes_read = 0;
  uint64_t t1 = get_current_time_millis();
  read_entry_bytes(entry, filesize, path,
                   entry->buffer + entry->next_buffer_pos,
                   want_bytes, current_file_pos, &bytes_read);
  uint64_t took = get_current_time_millis() - t1;

  if (bytes_read != want_bytes) {
    // File may be unreadable or changed size, either way, ignore it.
    LOG(L_PROGRESS, "error: read %" PRIu64 " bytes from [%s] but wanted %"
        PRIu32 " (%s)\n", bytes_read, path, want_bytes, strerror(errno));
    int before = head->list_size;
    int after = mark_path_entry_invalid(head, entry);
    int additional = before - 1 - after;
    if (additional > 0) {
      LOG(L_SKIPPED, "Defaulting %d additional files as unique\n", additional);
      increase_unique_counter(additional);
    }

  } else {

    entry->next_read_byte += bytes_read;
    entry->next_buffer_pos += bytes_read;

    if (consumed_block) {
      rv = 1;
      entry->next_read_block++;

      if (entry->next_read_block < entry->blocks->count) {

        uint64_t next_available_byte =
          entry->blocks->entry[entry->next_read_block].start_pos;

        // If file has a gap after current position, fill with zeroes as needed
        if (next_available_byte > entry->next_read_byte) {
          LOG(L_TRACE, "GAP next_available_byte: %" PRIu64
              ", entry->next_read_byte: %" PRIu64 "\n",
              next_available_byte, entry->next_read_byte);
          uint32_t gap_size = next_available_byte - entry->next_read_byte;
          uint32_t buf_space = entry->bufsize - entry->next_buffer_pos;
          uint32_t zeroes = gap_size;
          if (buf_space < zeroes) {
            zeroes = buf_space;
            filling_buffer = 1;
          }
          LOG(L_TRACE, "Gap after in [%s]: gap_size: %" PRIu32 ", buf_space: %"
              PRIu32 ", zeroes: %" PRIu32 "\n",
              path, gap_size, buf_space, zeroes);
          memset(entry->buffer + entry->next_buffer_pos, 0, zeroes);
          entry->next_buffer_pos += zeroes;
          entry->next_read_byte += zeroes;
        }
      } else {
        LOG(L_TRACE, "File completed [%s]\n", path);
      }
    }

    if (entry->next_read_byte >= filesize) {
      mark_path_entry_ready(head, entry);
      entry->data_in_buffer = entry->next_buffer_pos;
      head->sizelist->fully_read = 1;

    } else if (filling_buffer) {
      mark_path_entry_ready(head, entry);
      entry->data_in_buffer = entry->bufsize;
    }

    read_count++;
    int new_avg = avg_read_time + (took - avg_read_time) / read_count;
    avg_read_time = new_avg;

    LOG(L_TRACE, " read took %" PRIu64 "ms (count=%d avg=%d)\n",
        took, read_count, avg_read_time);
  }

  return rv;
}


/** ***************************************************************************
 * Reader thread.
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
  uint64_t size;
  int rlpos = 0;
  int done;
  int needy;
  int done_files = 0;
  int waiting_hash;
  int loop = 0;
  int submit_this_one;
  int invalid;
  int did_something;
  struct path_list_entry * pathlist_entry;
  struct path_list_head * pathlist_head;
  char path[DUPD_PATH_MAX];
  struct hasher_param * hasher_info = (struct hasher_param *)arg;
  int next_queue = 0;
  uint8_t block;
  int bfpct;

  pthread_setspecific(thread_name, self);
  LOG(L_THREADS, "Thread created\n");

  if (read_list_end == 0) {                                  // LCOV_EXCL_START
    LOG(L_INFO, "readlist is empty, nothing to read\n");
    return NULL;
  }                                                          // LCOV_EXCL_STOP

  do {
    rlpos = 0;
    needy = 0;
    done_files = 0;
    waiting_hash = 0;
    invalid = 0;
    did_something = 0;

    loop++;
    LOG(L_THREADS, "Starting read list loop %d\n", loop);

    do {

      rlentry = &read_list[rlpos];
      if (rlentry->done) {
        rlpos++;
        done_files++;
        continue;
      }

      pathlist_head = rlentry->pathlist_head;
      pathlist_entry = rlentry->pathlist_self;
      sizelist = pathlist_head->sizelist;
      submit_this_one = 0;

      d_mutex_lock(&sizelist->lock, "process_readlist");

      switch (pathlist_entry->state) {

      case FS_NEED_DATA:
        needy++;

        // Is this read list entry the block this path wants to read?
        block = pathlist_entry->next_read_block;
        if (pathlist_entry->blocks->entry[block].block == rlentry->block) {

          build_path(pathlist_entry, path);

          if (path[0] == 0) {
            printf("error: path zero len\n");
            exit(1);
          }

          size = sizelist->size;

          LOG(L_MORE_THREADS, "[%d] Entry %d (%d files of size %" PRIu64
              " in state %s): (reading pos %" PRIu64 " block %d) %s\n",
              loop, rlpos, pathlist_head->list_size, size,
              file_state(pathlist_entry->state),
              pathlist_entry->next_read_byte, block, path);

          if (fill_data_block(pathlist_head, pathlist_entry, path)) {
            rlentry->done = 1;
          }
          did_something++;

          if (pathlist_head->state == PLS_ALL_BUFFERS_READY) {
            submit_this_one = 1;
          }
        }
        break;

      case FS_DONE:
        done_files++;
        break;

      case FS_BUFFER_READY:
        waiting_hash++;
        break;

      case FS_INVALID:
        done_files++;
        invalid++;
        break;

      default:
        dump_path_list("read list reader, unexpected state",
                       pathlist_head->sizelist->size, pathlist_head, 1);
        exit(1);
      }

      d_mutex_unlock(&sizelist->lock);

      if (submit_this_one) {
        submit_path_list(next_queue, pathlist_head, hasher_info);
        next_queue = (next_queue + 1) % HASHER_THREADS;
      }

      rlpos++;

      bfpct = (int)(100 * stats_read_buffers_allocated / buffer_limit);
      if (bfpct > 99) {
        LOG(L_THREADS, "Buffer usage %d, flushing...\n", bfpct);
        size_list_flusher(hasher_info);
      }

    } while (rlpos < read_list_end);

    LOG(L_THREADS, "Completed loop %d: list size: %d worked: %d "
        "(NEED_DATA %d, NEED_HASH %d, INVALID %d, DONE %d)\n",
        loop, rlpos, did_something, needy, waiting_hash, invalid, done_files);

    done = done_files >= rlpos;

    if (!done) {
      if (!did_something) { usleep(1000 * 50); }
    }

  } while (!done);

  LOG(L_MORE_INFO, "DONE read list reader (%d loops)\n", loop);

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
  s_stats_size_list_count = 0;
  stats_size_list_avg = 0;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
struct size_list * add_to_size_list(uint64_t size,
                                    struct path_list_head * path_list)
{
  stats_size_list_avg = stats_size_list_avg +
    ( (size - stats_size_list_avg) / (s_stats_size_list_count + 1) );

  if (size_list_head == NULL) {
    size_list_head = new_size_list_entry(size, path_list);
    size_list_tail = size_list_head;
    s_stats_size_list_count = 1;
    return size_list_head;
  }

  struct size_list * new_entry = new_size_list_entry(size, path_list);
  size_list_tail->next = new_entry;
  size_list_tail = size_list_tail->next;
  s_stats_size_list_count++;
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
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void process_size_list(sqlite3 * dbh)
{
  pthread_t reader_thread;
  struct hasher_param hasher_info[HASHER_THREADS];
  int initial_size = 50;

  if (size_list_head == NULL) {
    return;
  }

  if (x_small_buffers) { initial_size = 2; }

  for (int n = 0; n < HASHER_THREADS; n++) {
    hasher_info[n].thread_num = n;
    hasher_info[n].done = 0;
    hasher_info[n].dbh = dbh;
    pthread_mutex_init(&hasher_info[n].queue_lock, NULL);
    pthread_cond_init(&hasher_info[n].queue_cond, NULL);
    hasher_info[n].queue_pos = -1;
    hasher_info[n].size = initial_size;
    hasher_info[n].queue =
      (struct path_list_head **)malloc(initial_size *
                                      sizeof(struct path_list_head *));
    for (int i = 0; i < initial_size; i++) {
      hasher_info[n].queue[i] = NULL;
    }
  }

  stats_round_start[ROUND1] = get_current_time_millis();

  // Start file reader thread
  LOG(L_THREADS, "Starting file reader thread...\n");
  d_create(&reader_thread, read_list_reader, &hasher_info);

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

  if (stats_read_buffers_allocated != 0) {
    printf("error: after round1 complete, buffers: %" PRIu64 "\n",
           stats_read_buffers_allocated);
    dump_size_list();
    exit(1);
  }

  if (current_open_files != 0) {
    printf("error: after processing complete, open files: %d\n",
           current_open_files);
    dump_size_list();
    exit(1);
  }

  for (int n = 0; n < HASHER_THREADS; n++) {
    free(hasher_info[n].queue);
  }

}
