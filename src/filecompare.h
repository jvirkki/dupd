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

#ifndef _DUPD_FILECOMPARE_H
#define _DUPD_FILECOMPARE_H

#include <sqlite3.h>


/** ***************************************************************************
 * Compare two files and publish to duplicates if that is the case.
 *
 * Parameters:
 *    dbh   - Database handle.
 *    path1 - Path of first file.
 *    path2 - Path of second file.
 *    size  - Size of the files.
 *
 * Return: none.
 *
 */
void compare_two_files(sqlite3 * dbh, char * path1, char * path2, off_t size);


/** ***************************************************************************
 * Compare three files and publish to duplicates if that is the case.
 *
 * Parameters:
 *    dbh   - Database handle.
 *    path1 - Path of first file.
 *    path2 - Path of second file.
 *    path3 - Path of third file.
 *    size  - Size of the files.
 *
 * Return: none.
 *
 */
void compare_three_files(sqlite3 * dbh,
                         char * path1, char * path2, char * path3, off_t size);


/** ***************************************************************************
 * Initialize.
 *
 */
void init_filecompare();


/** ***************************************************************************
 * Free any allocations.
 *
 */
void free_filecompare();


#endif
