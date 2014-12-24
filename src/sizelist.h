/*
  Copyright 2012 Jyri J. Virkki <jyri@virkki.com>

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

#include <sqlite3.h>


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
void add_to_size_list(long size, char * path_list);


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
void process_size_list(sqlite3 * dbh);


#endif
