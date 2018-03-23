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

#ifndef _DUPD_SCAN_H
#define _DUPD_SCAN_H

#include "dirtree.h"

#define SCAN_SIZE_UNKNOWN -42
#define SCAN_INODE_UNKNOWN 0
#define SCAN_DEV_UNKNOWN -1

extern pthread_mutex_t status_lock;
extern pthread_cond_t status_cond;


/** ***************************************************************************
 * Initialize memory structures for scan.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void init_scanlist();


/** ***************************************************************************
 * Free memory initialized with init_scanlist()
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void free_scanlist();


/** ***************************************************************************
 * Walk down the directory path given and process each file found.
 *
 * Parameters:
 *    dbh          - sqlite3 database handle.
 *    path         - The path to process. Must not be null or empty.
 *    dir_entry    - The dir tree entry for this directory.
 *                   Can be NULL when walk_dir used in non-scan operations.
 *    device       - device of the initial path root. Used when the
 *                   --one-file-system option is set to stay on that device.
 *    process_file - Function to call on each file as it is found.
 * Return: none
 *
 */
void walk_dir(sqlite3 * dbh, const char * path, struct direntry * dir_entry,
              dev_t device,
              int (*process_file)(sqlite3 *, uint64_t, ino_t, off_t, char *,
                                  char *, struct direntry *));


/** ***************************************************************************
 * This is the public entry point for scanning files.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void scan();


#endif
