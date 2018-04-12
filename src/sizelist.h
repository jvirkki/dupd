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

#ifndef _DUPD_SIZELIST_H
#define _DUPD_SIZELIST_H

#include <pthread.h>
#include <sqlite3.h>
#include <sys/types.h>

struct size_list {
  uint64_t size;
  struct path_list_head * path_list;
  int state;
  int fully_read;
  uint32_t buffers_filled;
  uint64_t bytes_read;
  pthread_mutex_t lock;
  struct size_list * next;
  struct size_list * dnext;
};

// Size list states. TODO remove
#define SLS_NEED_BYTES_ROUND_1 88
#define SLS_READY_1 89
#define SLS_NEED_BYTES_ROUND_2 90
#define SLS_READY_2 91
#define SLS_NEEDS_ROUND_2 92
#define SLS_DONE 94
#define SLS_R2_HASH_ME 96
#define SLS_R2_HASH_ME_FINAL 97
#define SLS_R2_HASH_DONE 98
#define SLS_R2_READ_MORE 99
#define SLS_R2_READ_FINAL 100
#define SLS_R2_HASHER_IGNORE 101
#define SLS_DELETED 102


/** ***************************************************************************
 * Print progress on set processing.
 *
 * Parameters:
 *     total - Total sets processed.
 *     files - Cound of files in set.
 *     size  - Size of files in set.
 *
 * Return: none
 *
 */
void show_processed(int total, int files, uint64_t size);


/** ***************************************************************************
 * Initialize size list.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void init_size_list();


/** ***************************************************************************
 * Free size list.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void free_size_list();


/** ***************************************************************************
 * Adds a size_list entry to the tail of the list.
 *
 * Parameters:
 *    size      - Size
 *    path_list - Head of path list of files of this size
 *
 * Return: An intialized/allocated size list node.
 *
 */
struct size_list * add_to_size_list(uint64_t size,
                                    struct path_list_head * path_list);


/** ***************************************************************************
 * An internal testing version of process_size_list. Produces output
 * showing how many blocks needed to discard potential duplicates.
 * Not intended for normal usage.
 *
 * Parameters:
 *    dbh - Database pointer.
 *
 * Return: none
 *
 */
void analyze_process_size_list(sqlite3 * dbh);


/** ***************************************************************************
 * Process the size list. The core of the duplicate detection is done
 * from here.
 *
 * This function (and its worker threads) read data as needed and filter
 * the size list produced during scanning down to known duplicate groups.
 *
 * Duplicates are published to the sqlite database, unless write_db is false.
 *
 * Parameters:
 *    dbh - Database pointer.
 *
 * Return: none
 *
 */
void process_size_list(sqlite3 * dbh);

#endif
