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

#ifndef _DUPD_SIZETREE_H
#define _DUPD_SIZETREE_H

#include <sqlite3.h>

#include "dirtree.h"


/** ***************************************************************************
 * Add the given path to the size tree. Also adds the path to the path list.
 *
 * Parameters:
 *    dbh       - sqlite3 database handle (not used, set to NULL).
 *    inode     - The inode of this file (or SCAN_INODE_UNKNOWN).
 *    size      - Size of this file (or SCAN_SIZE_UNKNOWN).
 *    path      - Path of this file.
 *    filename  - Name of this file, relative to dir_entry.
 *    dir_entry - Directory entry of the dir containing this file.
 *
 * Return: none (int to comply with callback prototype)
 *
 */
int add_file(sqlite3 * dbh,
             ino_t inode, off_t size,  char * path,
             char * filename, struct direntry * dir_entry);


/** ***************************************************************************
 * Asynchronously add the given path to the size tree. Also adds the path
 * to the path list.
 *
 * This is equivalent to add_file(), used when running in threaded scan mode.
 *
 * Parameters:
 *    dbh       - sqlite3 database handle (not used, set to NULL).
 *    inode     - The inode of this file (or SCAN_INODE_UNKNOWN).
 *    size      - Size of this file (or SCAN_SIZE_UNKNOWN).
 *    path      - Path of this file.
 *    filename  - Name of this file, relative to dir_entry.
 *    dir_entry - Directory entry of the dir containing this file.
 *
 * Return: none (int to comply with callback prototype)
 *
 */
int add_queue(sqlite3 * dbh,
              ino_t inode, off_t size, char * path,
              char * filename, struct direntry * dir_entry);


/** ***************************************************************************
 * Walk through the (presumably completed) size tree to identify size
 * nodes corresponding to only one path. Save these unique files to
 * the database.
 *
 * Parameters:
 *    dbh  - sqlite3 database handle.
 *
 * Return: none
 *
 */
void find_unique_sizes(sqlite3 * dbh);


/** ***************************************************************************
 * Initialize.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void init_sizetree();


/** ***************************************************************************
 * When running in threaded scan mode, indicate that scan operation is done.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void scan_done();


/** ***************************************************************************
 * Walk through the size tree and free everything.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void free_size_tree();


#endif
