/*
  Copyright 2018 Jyri J. Virkki <jyri@virkki.com>

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

#include "hash.h"
#include "hashers.h"
#include "hashlist.h"
#include "main.h"
#include "paths.h"
#include "sizelist.h"
#include "stats.h"
#include "utils.h"


/** ***************************************************************************
 * Update hash with new data in buffer.
 *
 * Parameters:
 *    entry    - Compute hash of this entry given its latest data.
 *    hash_out - Return hash in this buffer.
 *
 * Return: none
 *
 */
static void update_node_hash(struct path_list_entry * node, char * hash_out)
{
  // If this is the first block of data being hashed, need to initialize
  if (node->hash_ctx == NULL) {
    node->hash_ctx =  hash_fn_buf_init();
  }

  hash_fn_buf_update(node->hash_ctx, node->buffer, node->data_in_buffer);
  hash_fn_get_partial(node->hash_ctx, hash_out);
}


/** ***************************************************************************
 * Helper function to process files from a size node to a hash list.
 * The data buffers are freed as each one is consumed.
 *
 * Parameters:
 *    dbh       - db handle for saving duplicates/uniques
 *    size_node - Process this size node (and associated path list).
 *    hl        - Save paths to this hash list.
 *
 * Return: 1 if this path list was completed (either confirmed duplicates
 *         or ruled out possibility of being duplicates).
 *
 */
static int build_hash_list_round(sqlite3 * dbh,
                                 struct size_list * size_node,
                                 struct hash_table * hl)
{
  char hash_out[HASH_MAX_BUFSIZE];
  struct path_list_entry * node;
  int completed = 0;
  uint32_t prev_buffer = 0;

  node = pb_get_first_entry(size_node->path_list);

  LOG(L_TRACE, "Building hash list for size %" PRIu64 "\n", size_node->size);

  // Build hash list for all files which have a buffer filled.
  // That's the usual case but some files may be in other states such as
  // FS_INVALID if they have been previously discarded.
  do {

    if (node->state == FS_BUFFER_READY) {
      update_node_hash(node, hash_out);
      add_to_hash_table(hl, node, hash_out);
      node->state = FS_NEED_DATA;

      if (prev_buffer > 0 && node->data_in_buffer != prev_buffer) {
        printf("error: inconsistent amount of data in buffers\n");
        dump_path_list("bad state", size_node->size, size_node->path_list, 1);
        exit(1);
      }

      prev_buffer = node->data_in_buffer;
      node->data_in_buffer = 0;
      node->next_buffer_pos = 0;
    }

    node = node->next;
  } while (node != NULL);

  size_node->path_list->state = PLS_NEED_DATA;
  size_node->path_list->buffer_ready = 0;

  LOG_TRACE {
    LOG(L_TRACE, "Contents of hash list hl:\n");
    print_hash_table(hl);
  }

  // Remove the uniques seen
  skim_uniques(size_node->path_list, hl);

  // If no potential dups after this round, we're done!
  if (!hash_table_has_dups(hl)) {
    LOG(L_TRACE, "No potential dups left, done!\n");
    size_node->path_list->state = PLS_DONE;
    completed = 1;

  } else {
    // If by now we already have a full hash, publish and we're done!
    if (size_node->fully_read) {
      LOG(L_TRACE, "Some dups confirmed, here they are:\n");
      publish_duplicate_hash_table(dbh, hl, size_node->size);
      size_node->path_list->state = PLS_DONE;
      completed = 1;
      increase_dup_counter(size_node->path_list->list_size);
    }
  }

  if (size_node->path_list->hash_passes == 0) {
    increase_sets_first_read();
    if (completed) { increase_sets_first_read_completed(); }
  }

  if (size_node->path_list->hash_passes < 255) {
    size_node->path_list->hash_passes++;
  }

  if (!completed) {

    // Will be reading more, so increase buffer size for next time around
    if (use_hash_cache) {
      size_node->path_list->wanted_bufsize = K512;
    } else {
      // If not using the cache, may need to read very large files
      if (size_node->path_list->hash_passes == 1) {
        size_node->path_list->wanted_bufsize = MB2;
      } else if (size_node->path_list->hash_passes > 2) {
        size_node->path_list->wanted_bufsize = MB16;
      }
    }

    if (size_node->path_list->wanted_bufsize > size_node->size) {
      size_node->path_list->wanted_bufsize = size_node->size;
    }
  }

  return completed;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void * round1_hasher(void * arg)
{
  struct hasher_param * info = (struct hasher_param *)arg;
  struct path_list_head * entry = NULL;
  struct size_list * size_node;
  sqlite3 * dbh = info->dbh;
  int set_completed;
  int path_count;
  int done = 0;
  char self[80];
  struct hash_table * ht = init_hash_table();

  snprintf(self, 80,  "      [R1-hasher-%d] ", info->thread_num);
  pthread_setspecific(thread_name, self);
  LOG(L_THREADS, "Thread created\n");

  do {

    entry = NULL;

    // Get an entry from my queue or wait for it.
    // The reader thread adds path list entries to my work queue.

    d_mutex_lock(&info->queue_lock, "R1-hasher-get");

    while (info->done == 0 && info->queue_pos < 0) {
      d_cond_wait(&info->queue_cond, &info->queue_lock);
    }

    if (info->queue_pos >= 0) {
      entry = info->queue[info->queue_pos];
      info->queue[info->queue_pos] = NULL;
      LOG(L_THREADS, "Grabbed path list from queue(%d) position %d\n",
          info->thread_num, info->queue_pos);
      info->queue_pos--;
      stats_hasher_queue_len[info->thread_num] = info->queue_pos;
    }

    if (info->done == 1 && info->queue_pos == -1) {
      LOG(L_THREADS, "My queue is empty (pos=%d) and done=%d.\n",
          info->queue_pos, info->done);
      done = 1;
    }

    d_mutex_unlock(&info->queue_lock);

    if (entry != NULL) {

      size_node = entry->sizelist;
      d_mutex_lock(&size_node->lock, "hasher");

      LOG_THREADS {
        LOG(L_THREADS, "Set (%d files of size %" PRIu64 ") pass %d\n",
            size_node->path_list->list_size, size_node->size,
            1 + size_node->path_list->hash_passes);
      }

      reset_hash_table(ht);
      set_completed = build_hash_list_round(dbh, size_node, ht);

      if (set_completed) {
        path_count = size_node->path_list->list_size;
        show_processed(s_stats_size_list_count, path_count, size_node->size);
      }

      d_mutex_unlock(&size_node->lock);

    } // if entry != NULL
  } while (!done);

  free_hash_table(ht);

  LOG(L_THREADS, "DONE\n");

  return NULL;
}
