/*
  Copyright 2012-2021 Jyri J. Virkki <jyri@virkki.com>

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
  struct path_list_head * path_list;
  struct size_list * next;
  uint64_t size;
  int fully_read;
  pthread_mutex_t lock;
};

extern struct size_list * size_list_head;


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
 * Process the size list. The core of the duplicate detection is done
 * from here.
 *
 * This function (and its worker threads) read data as needed and filter
 * the size list produced during scanning down to known duplicate groups.
 *
 * Duplicates are published to the sqlite database.
 *
 * Parameters:
 *    dbh - Database pointer.
 *
 * Return: none
 *
 */
void process_size_list(sqlite3 * dbh);

#endif
