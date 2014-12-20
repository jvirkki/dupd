/*
  Copyright 2012-2014 Jyri J. Virkki <jyri@virkki.com>

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


/** ***************************************************************************
 * Add the given path to the size tree. Also adds the path to the path list.
 *
 * Parameters:
 *    dbh  - sqlite3 database handle.
 *    size - Size of this file.
 *    path - Path of this file.
 *
 * Return: none (int to comply with callback prototype)
 *
 */
int add_file(sqlite3 * dbh, long size, char * path);


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


#endif
