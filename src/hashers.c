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

#include "hashers.h"
#include "hashlist.h"
#include "main.h"
#include "paths.h"
#include "sizelist.h"
#include "stats.h"
#include "utils.h"


/** ***************************************************************************
 * Helper function to process files from a size node to a hash list.
 * The data buffers are freed as each one is consumed.
 *
 * Parameters:
 *    dbh       - db handle for saving duplicates/uniques
 *    size_node - Process this size node (and associated path list.
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
  struct path_list_entry * node;
  int completed = 0;

  node = pb_get_first_entry(size_node->path_list);

  // For small files, they may have been fully read already
  d_mutex_lock(&stats_lock, "build hash list stats");
  if (size_node->fully_read) { stats_sets_full_read[1]++; }
  else { stats_sets_part_read[1]++; }
  d_mutex_unlock(&stats_lock);

  LOG(L_TRACE, "Building hash list for size %ld\n", size_node->size);

  // Build hash list for all files which have a buffer filled.
  // That's the usual case but some files may be in other states such as
  // FS_INVALID if they have been previously discarded.
  do {
    if (node->state == FS_R1_BUFFER_FILLED) {
      add_hash_table_from_mem(hl, node, node->buffer, size_node->bytes_read);
      free(node->buffer);
      dec_stats_read_buffers_allocated(size_node->bytes_read);
      node->buffer = NULL;
      node->state = FS_R1_DONE;
    }
    node = node->next;
  } while (node != NULL);

  LOG_TRACE {
    LOG(L_TRACE, "Contents of hash list hl:\n");
    print_hash_table(hl);
  }

  // Remove the uniques seen
  int skimmed = skim_uniques(dbh, hl, save_uniques);
  if (skimmed) {
    size_node->path_list->list_size -= skimmed;
  }

  // If no potential dups after this round, we're done!
  if (!hash_table_has_dups(hl)) {
    LOG(L_TRACE, "No potential dups left, done!\n");
    size_node->path_list->state = PLS_DONE;
    completed = 1;
    d_mutex_lock(&stats_lock, "build hash list stats");
    stats_sets_dup_not[ROUND1]++;
    d_mutex_unlock(&stats_lock);

  } else {
    // If by now we already have a full hash, publish and we're done!
    if (size_node->fully_read) {
      LOG(L_TRACE, "Some dups confirmed, here they are:\n");
      publish_duplicate_hash_table(dbh, hl, size_node->size, ROUND1);
      size_node->path_list->state = PLS_DONE;
      completed = 1;
      d_mutex_lock(&stats_lock, "build hash list stats");
      stats_sets_dup_done[ROUND1]++;
      d_mutex_unlock(&stats_lock);
    }
  }

  size_node->buffers_filled = 0;

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
  int set = 0;
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

    // Send signal because the reader thread can sometimes wait if
    // the queue became full, so need to wake it up in that case.
    // TODO: remove because reader should now wait, realloc there.
    d_cond_signal(&info->queue_cond);

    d_mutex_unlock(&info->queue_lock);

    if (entry != NULL) {

      if (entry->state != PLS_R1_BUFFERS_FULL) {
        printf("error: round1_hasher bad path list state %s\n",
               pls_state(entry->state));
        log_level = L_TRACE;
        dump_path_list("bad state", 0, entry);
        exit(1);
      }

      size_node = entry->sizelist;
      LOG_THREADS {
        set++;
        LOG(L_THREADS, "SET %d size:%ld state:%s\n", set,
            (long)size_node->size, pls_state(entry->state));
      }

      reset_hash_table(ht);
      stats_sets_processed[ROUND1]++;
      set_completed = build_hash_list_round(dbh, size_node, ht);

      if (!size_node->fully_read) {
        stats_one_block_hash_first++;
      }

      if (!set_completed) {
        entry->state = PLS_R2_NEEDED;
        size_node->state = SLS_NEEDS_ROUND_2; // TODO remove sizenode state

      } else {
        if (size_node->fully_read) {
          stats_full_hash_first++;
        }

        path_count = size_node->path_list->list_size;
        show_processed(stats_size_list_count, path_count, size_node->size);
      }

    } // if entry != NULL
  } while (!done);

  free_hash_table(ht);

  LOG(L_THREADS, "DONE\n");

  return NULL;
}
