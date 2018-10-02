/*
  Copyright 2012-2014,2018 Jyri J. Virkki <jyri@virkki.com>

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

#define CACHE_FILE_NOT_PRESENT 5
#define CACHE_HASH_NOT_PRESENT 6
#define CACHE_HASH_FOUND 7


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
 * Open the sqlite hash cache database.
 *
 * Parameters:
 *    path  - path to the file containing the hash database
 *
 * Return: none
 *
 */
void open_cache_database(char * path);


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
 * Close the sqlite hash cache database.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void close_cache_database();


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


/** ***************************************************************************
 * Find the hash of a given path in the hash cache.
 *
 * If the path is not in the db, just returns CACHE_FILE_NOT_PRESENT.
 *
 * If the path is present but either the size or timestamp do not
 * match the values in the db (that is, the file has been modified after
 * the db entry was created) then all the hash values present in the db
 * are deleted (as they are no longer valid) and the size and timestamp
 * are updated to the current values. Function returns CACHE_HASH_NOT_PRESENT.
 *
 * If the path is present and up to date but no matching hash_alg hash is
 * present, returns CACHE_HASH_NOT_PRESENT.
 *
 * If path is present and up to date and desired hash_alg is found, the
 * hash is copied into 'hash' buffer (which caller must have allocated)
 * and CACHE_HASH_FOUND is returned.
 *
 * Parameters:
 *    path      - Path of the file to check.
 *    file_id   - On success, the file_id (db row) is set here.
 *    hashbuf   - On success, the hash is copied here (caller allocated).
 *
 * Return:
 *    CACHE_FILE_NOT_PRESENT - If path not in cache db.
 *    CACHE_HASH_NOT_PRESENT - If path is present but desired hash is not.
 *    CACHE_HASH_FOUND       - Hash found (populated into 'hash' buffer).
 *
 */
int cache_db_find_entry_id(char * path, uint64_t * file_id, char * hashbuf);


/** ***************************************************************************
 * Find the hash of a given path in the hash cache.
 *
 * Same as cache_db_find_entry_id() but the file_id is not returned.
 *
 * Parameters:
 *    path      - Path of the file to check.
 *    hashbuf   - On success, the hash is copied here (caller allocated).
 *
 * Return:
 *    CACHE_FILE_NOT_PRESENT - If path not in cache db.
 *    CACHE_HASH_NOT_PRESENT - If path is present but desired hash is not.
 *    CACHE_HASH_FOUND       - Hash found (populated into 'hash' buffer).
 *
 */
int cache_db_find_entry(char * path, char * hashbuf);


/** ***************************************************************************
 * Find if the hash of a given path is in the hash cache.
 *
 * Same as cache_db_find_entry_id() but neither file_id nor the hash
 * is returned.
 *
 * Parameters:
 *    path      - Path of the file to check.
 *
 * Return:
 *    CACHE_FILE_NOT_PRESENT - If path not in cache db.
 *    CACHE_HASH_NOT_PRESENT - If path is present but desired hash is not.
 *    CACHE_HASH_FOUND       - Hash found (populated into 'hash' buffer).
 *
 */
int cache_db_check_entry(char * path);


/** ***************************************************************************
 * Add hash for a path to the hash cache db.
 *
 * If the corresponding hash is already in the db and up to date, do nothing.
 *
 * If data in the db is not up to date, it is removed and replaced.
 *
 * Otherwise, adds the path to the db if not already there and then adds
 * the given hash/hash_alg pair to the db.
 *
 * Parameters:
 *    path      - Path of the file to add hash.
 *    hash      - The hash to save is here.
 *    hash_len  - Lenght of 'hash' (bytes).
 *
 * Return: none
 *
 */
void cache_db_add_entry(char * path, char * hash, int hash_len);

#endif
