/*
  Copyright 2012-2023 Jyri J. Virkki <jyri@virkki.com>

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
#include <stdint.h>
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
 * Close the sqlite database.
 *
 * Parameters:
 *    dbh - sqlite3 database handle.
 *
 * Return: none
 *
 */
void close_database(sqlite3 * dbh);


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
void duplicate_to_db(sqlite3 * dbh, int count, uint64_t size, char * paths);


/** ***************************************************************************
 * Remove a duplicate entry from the database.
 *
 * Parameters:
 *    dbh - Database handle.
 *    id  - id of entry to delete
 *
 * Return: none.
 *
 */
void delete_duplicate_entry(sqlite3 * dbh, int id);


/** ***************************************************************************
 * Write unique file info to database.
 *
 * Parameters:
 *    dbh  - Database handle.
 *    path - Path of the unique file.
 *    msg  - Debug string for verbose reporting, who detected this unique.
 *
 * Return: none.
 *
 */
void unique_to_db(sqlite3 * dbh, char * path, char * msg);


/** ***************************************************************************
 * Check files table (if available) to see if this file is unique.
 *
 * Parameters:
 *    dbh  - Database handle.
 *    path - Path of the file to check.
 *
 * Return:
 *    0 - Not present in table.
 *    1 - Known unique, present in table.
 *
 */
int is_known_unique(sqlite3 * dbh, char * path);


/** ***************************************************************************
 * Print to stdout all the unique files from 'files' table which fall under
 * the given 'path'.
 *
 * Parameters:
 *    dbh  - Database handle.
 *    path - Subdirectory tree path to consider.
 *
 * Return: none
 *
 */
void print_all_uniques(sqlite3 * dbh, char * path);


/** ***************************************************************************
 * Pre-allocate memory used by get_known_duplicates().
 *
 * Parameters: none
 * Return: none
 *
 */
void init_get_known_duplicates();


/** ***************************************************************************
 * Retrieve the known duplicates of 'path' from the database.
 *
 * Parameters:
 *    dbh  - Database handle.
 *    path - Path of the file to check.
 *    dups - Number of duplicates found is returned here (zero or more)
 *
 * Return:
 *    Array of strings (size in 'dups'), containing the paths of each
 *    duplicate found in the db. Caller must free with free_known_duplicates.
 *
 */
char * * get_known_duplicates(sqlite3  *dbh, char * path, int * dups);


/** ***************************************************************************
 * Free memory allocated by init_get_known_duplicates()
 *
 * Parameters: none
 * Return: none
 *
 */
void free_get_known_duplicates();


#endif
