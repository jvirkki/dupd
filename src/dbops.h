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

#ifndef _DUPD_DBOPS_H
#define _DUPD_DBOPS_H

#include <sqlite3.h>
#include <sys/types.h>


/** ***************************************************************************
 * Open the sqlite database.
 *
 * Parameters:
 *    path  - path to the file containing the database
 *    newdb - if true, create a new empty db (deleting old one if necessary)
 *
 * Return: sqlite3 db handle, needed by everything else from here on.
 *
 */
sqlite3 * open_database(char * path, int newdb);


/** ***************************************************************************
 * Begin a database transaction.
 *
 * By default each insert is a transaction and gets synced to disk. This makes
 * doing many insertions painfully slow. An alternative is to surround a group
 * of insert operations into a transaction by calling begin/commit transaction.
 *
 * Parameters:
 *    dbh - sqlite3 database handle.
 *
 * Return: none
 *
 */
void begin_transaction(sqlite3 * dbh);


/** ***************************************************************************
 * Commit a transaction started earlier with begin_transaction().
 *
 * Parameters:
 *    dbh - sqlite3 database handle.
 *
 * Return: none
 *
 */
void commit_transaction(sqlite3 * dbh);


/** ***************************************************************************
 * Checks the return value of sqlite function calls. If the return value
 * rv does not match the expected return code, close db and exit.
 *
 * Parameters:
 *    rv   - Value returned by the call we made
 *    code - Expected return code
 *    dbh  - sqlite3 database handle
 *
 * Return: none
 *
 */
void rvchk(int rv, int code, char * line, sqlite3 * dbh);


/** ***************************************************************************
 * Write duplicate row to database.
 *
 * Parameters:
 *    dbh   - Database handle.
 *    count - Number of paths.
 *    size  - Size of the files.
 *    paths - Comma-separated list of the paths.
 *
 * Return: none.
 *
 */
void duplicate_to_db(sqlite3 * dbh, int count, off_t size, char * paths);


#endif
