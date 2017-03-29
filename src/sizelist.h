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

#ifndef _DUPD_SIZELIST_H
#define _DUPD_SIZELIST_H

#include <pthread.h>
#include <sqlite3.h>

struct size_list {
  off_t size;
  char * path_list;
  int state;
  int fully_read;
  uint32_t buffers_filled;
  off_t bytes_read;
  pthread_mutex_t lock;
  struct size_list * next;
  struct size_list * dnext;
};


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
struct size_list * add_to_size_list(off_t size, char * path_list);


/** ***************************************************************************
 * Process the size list. The core of the duplicate detection is done
 * from here.
 *
 * This function walks down the size list produced during scanning.
 *
 * Duplicates are published to the sqlite database, unless write_db is false.
 *
 * Parameters:
 *    dbh - Database pointer.
 *
 * Return: none
 *
 */
//void process_size_list(sqlite3 * dbh);


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
 * Process the size list in two threads using read list ordering for HDDs.
 *
 * Otherwise same as process_size_list() above.
 *
 */
//void threaded_process_size_list_hdd(sqlite3 * dbh);


/** ***************************************************************************
 * Process the size list in two threads.
 *
 * Otherwise same as process_size_list() above.
 *
 */
//void threaded_process_size_list(sqlite3 * dbh);


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
