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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "dbops.h"
#include "hash.h"
#include "main.h"
#include "stats.h"
#include "utils.h"

#define ONE_MB_BYTES 1048576

static char * * known_dup_path_list = NULL;
static int known_dup_path_list_size = 512;
static int known_dup_path_list_first = 1;
static sqlite3_stmt * stmt_is_known_unique = NULL;
static sqlite3_stmt * stmt_duplicate_to_db = NULL;
static sqlite3_stmt * stmt_delete_duplicate = NULL;
static sqlite3_stmt * stmt_unique_to_db = NULL;
static sqlite3_stmt * stmt_get_known_duplicates = NULL;

static pthread_mutex_t dbh_lock = PTHREAD_MUTEX_INITIALIZER;


/** ***************************************************************************
 * Runs a single SQL statement. Only works for self-contained SQL strings
 * (not prepared statements) which return no results.
 *
 * Parameters:
 *    dbh - sqlite3 database handle.
 *    sql - SQL to run
 *
 * Return: none
 *
 */
static void single_statement(sqlite3 * dbh, const char * sql)
{
  sqlite3_stmt * statement = NULL;
  int rv;

  LOG(L_TRACE, "SQL: [%s]\n", sql);

  rv = sqlite3_prepare_v2(dbh, sql, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);

  rv = sqlite3_step(statement);
  rvchk(rv, SQLITE_DONE, "Can't step: (rv=%d) %s\n", dbh);

  sqlite3_finalize(statement);
}


/** ***************************************************************************
 * Create the hash cache tables.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
static void initialize_cache_database()
{
  single_statement(cache_dbh, "CREATE TABLE files "
                              "(id INTEGER PRIMARY KEY, "
                              "path TEXT NOT NULL UNIQUE, size INTEGER, "
                              "timestamp INTEGER)");

  single_statement(cache_dbh, "CREATE TABLE hashes "
                              "(id INTEGER, alg INTEGER, hash BLOB, "
                              "PRIMARY KEY(id,alg), "
                              "FOREIGN KEY(id) REFERENCES files(id) "
                              ")");
}


/** ***************************************************************************
 * Create the tables used by dupd.
 *
 * Parameters:
 *    dbh         - sqlite3 database handle.
 *
 * Return: none
 *
 */
static void initialize_database(sqlite3 * dbh)
{
  single_statement(dbh, "CREATE TABLE duplicates "
                        "(id INTEGER PRIMARY KEY, count INTEGER, "
                        "each_size INTEGER, paths TEXT)");

  single_statement(dbh, "CREATE TABLE meta "
                        "(hidden INTEGER, version TEXT, "
                        "dbtime INTEGER, hardlinks TEXT)");

  // Save settings to meta table for future reference

  static sqlite3_stmt * stmt;
  const char * sql = "INSERT INTO meta (hidden, version, "
                     "dbtime, hardlinks) "
                     "VALUES (?, ?, ?, ?)";

  int rv = sqlite3_prepare_v2(dbh, sql, -1, &stmt, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);

  rv = sqlite3_bind_int(stmt, 1, scan_hidden);
  rvchk(rv, SQLITE_OK, "Can't bind hidden: %s\n", dbh);

  rv = sqlite3_bind_text(stmt, 2, DUPD_VERSION, -1, SQLITE_STATIC);
  rvchk(rv, SQLITE_OK, "Can't bind version: %s\n", dbh);

  uint64_t now = get_current_time_millis();
  rv = sqlite3_bind_int64(stmt, 3, now);
  rvchk(rv, SQLITE_OK, "Can't bind current dbtime: %s\n", dbh);

  if (hardlink_is_unique) {
    rv = sqlite3_bind_text(stmt, 4, "ignore", -1, SQLITE_STATIC);
  } else {
    rv = sqlite3_bind_text(stmt, 4, "normal", -1, SQLITE_STATIC);
  }
  rvchk(rv, SQLITE_OK, "Can't bind hardlinks: %s\n", dbh);

  rv = sqlite3_step(stmt);
  rvchk(rv, SQLITE_DONE, "tried to set meta data: %s\n", dbh);

  sqlite3_finalize(stmt);
}


/** ***************************************************************************
 * Delete all hash entries for a path and update size/timestamp info.
 *
 * Called when the file has changed, which means all the stored hashes are
 * now invalid.
 *
 * Parameters:
 *    path      - Path of the file to add hash.
 *    file_id   - The row id of path in the db.
 *    size      - Current size of the file.
 *    timestamp - Current modified time (st_mtime) of the file.
 *
 * Return: none
 *
 */
static void cache_db_scrub_entry(char * path, uint64_t file_id,
                                 uint64_t size, uint32_t timestamp)
{
  static char * sql = "DELETE FROM hashes WHERE id=?";
  static char * sqlu = "UPDATE files SET size=?, timestamp=? WHERE id=?";
  sqlite3_stmt * statement = NULL;
  int rv;

  LOG(L_MORE_INFO, "cache_db_scrub_entry: delete all hashes for file_id: %"
      PRIu64 " [%s]\n", file_id, path);

  // Delete all hash entries for this file

  rv = sqlite3_prepare_v2(cache_dbh, sql, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", cache_dbh);

  rv = sqlite3_bind_int(statement, 1, file_id);
  rvchk(rv, SQLITE_OK, "Can't bind file_id: %s\n", cache_dbh);

  rv = sqlite3_step(statement);
  sqlite3_finalize(statement);

  // Update the size and timestamp to new values

  rv = sqlite3_prepare_v2(cache_dbh, sqlu, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", cache_dbh);

  rv = sqlite3_bind_int(statement, 1, size);
  rvchk(rv, SQLITE_OK, "Can't bind size: %s\n", cache_dbh);

  rv = sqlite3_bind_int(statement, 2, timestamp);
  rvchk(rv, SQLITE_OK, "Can't bind timestamp: %s\n", cache_dbh);

  rv = sqlite3_bind_int(statement, 3, file_id);
  rvchk(rv, SQLITE_OK, "Can't bind id: %s\n", cache_dbh);

  rv = sqlite3_step(statement);
  sqlite3_finalize(statement);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
sqlite3 * open_database(char * path, int newdb)
{
  sqlite3 * dbh = NULL;
  int rv;

  rv = file_exists(path);

  if (newdb && rv == 1) {       /* need to delete old one */
    rv = unlink(path);
    if (rv != 0) {                                           // LCOV_EXCL_START
      char line[DUPD_PATH_MAX];
      snprintf(line, DUPD_PATH_MAX, "unlink %s", path);
      perror(line);
      exit(1);
    }                                                        // LCOV_EXCL_STOP
  }

  if (!newdb && rv == 0) {                                   // LCOV_EXCL_START
    printf("Unable to open %s for reading...\n", path);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  rv = sqlite3_open(path, &dbh);
  rvchk(rv, SQLITE_OK, "Can't open database: %s\n", dbh);

  if (newdb) {
    initialize_database(dbh);
    LOG(L_INFO, "Done initializing new database [%s]\n", path);
  }

  // Load meta info from database

  sqlite3_stmt * statement = NULL;
  char * sql = "SELECT hidden, version, dbtime, hardlinks "
               "FROM meta";

  rv = sqlite3_prepare_v2(dbh, sql, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);

  rv = sqlite3_step(statement);
  if (rv != SQLITE_ROW) {                                    // LCOV_EXCL_START
    printf("Error reading meta table!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  scan_hidden = sqlite3_column_int(statement, 0);
  LOG(L_PROGRESS, "Set scan_hidden from db to %d\n", scan_hidden);

  char * db_version = (char *)sqlite3_column_text(statement, 1);
  if (strcmp(db_version, DUPD_VERSION)) {                    // LCOV_EXCL_START
    printf("\n\n");
    printf("*** WARNING: database version %s\n", db_version);
    printf("*** does not match dupd version %s\n", DUPD_VERSION);
    printf("*** Will continue running and hope for the best but\n");
    printf("*** data may be incorrect and/or dupd may crash!\n");
    printf("*** Recommendation is to re-run dupd scan first.\n");
    printf("\n\n");
  }                                                          // LCOV_EXCL_STOP

  uint64_t db_create_time = (uint64_t)sqlite3_column_int64(statement, 2);
  LOG(L_PROGRESS, "database create time %" PRIu64 "\n", db_create_time);

  uint64_t expiration = db_create_time + 1000L * db_warn_age_seconds;
  uint64_t now = get_current_time_millis();
  if (now > expiration) {
    uint64_t age = (now - db_create_time) / 1000 / 60 / 60;
    printf("WARNING: database is %" PRIu64" hours old, may be stale!\n", age);
  }

  // If opening an existing db which was created with hardlinks=ignore,
  // make sure we're not now running with the same option set (that would
  // cause confusing output for the file operations (file|ls|dups|uniques).

  if (!newdb && hardlink_is_unique) {
    char * hardlinks = (char *)sqlite3_column_text(statement, 3);
    if (!strcmp(hardlinks, "ignore")) {
      printf("error: scan was already performed with --hardlink-is-unique\n");
      exit(1);
    }
  }

  sqlite3_finalize(statement);

  return dbh;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void open_cache_database(char * path)
{
  int rv;
  int newdb = 0;

  if (!file_exists(path)) { newdb = 1; }

  rv = sqlite3_open(path, &cache_dbh);
  rvchk(rv, SQLITE_OK, "Can't open database: %s\n", cache_dbh);

  if (newdb) {
    initialize_cache_database(cache_dbh);
    LOG(L_INFO, "Done initializing new cache database [%s]\n", path);
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void close_database(sqlite3 * dbh)
{
  if (dbh == NULL) {                                         // LCOV_EXCL_START
    return;
  }                                                          // LCOV_EXCL_STOP

  // Need to finalize all prepared statements in order to close db cleanly.
  if (stmt_is_known_unique != NULL) {
    sqlite3_finalize(stmt_is_known_unique);
  }

  if (stmt_duplicate_to_db != NULL) {
    sqlite3_finalize(stmt_duplicate_to_db);
  }

  if (stmt_unique_to_db != NULL) {
    sqlite3_finalize(stmt_unique_to_db);
  }

  if (stmt_delete_duplicate != NULL) {
    sqlite3_finalize(stmt_delete_duplicate);
  }

  if (stmt_get_known_duplicates != NULL) {
    sqlite3_finalize(stmt_get_known_duplicates);
  }

  int rv = sqlite3_close(dbh);
  if (rv == SQLITE_OK) {
    LOG(L_MORE_INFO, "closed database\n");
    return;
  }

  LOG(L_PROGRESS, "warning: unable to close database!\n");
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void close_cache_database()
{
  if (cache_dbh == NULL) {                                   // LCOV_EXCL_START
    return;
  }                                                          // LCOV_EXCL_STOP

  int rv = sqlite3_close(cache_dbh);
  cache_dbh = NULL;
  if (rv == SQLITE_OK) {
    LOG(L_MORE_INFO, "closed cache database\n");
    return;
  }

  LOG(L_PROGRESS, "warning: unable to close cache database!\n");
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void begin_transaction(sqlite3 * dbh)
{
  single_statement(dbh, "BEGIN EXCLUSIVE TRANSACTION");
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void commit_transaction(sqlite3 * dbh)
{
  single_statement(dbh, "COMMIT TRANSACTION");
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void rvchk(int rv, int code, char * line, sqlite3 * dbh)
{
  if (rv != code) {                                          // LCOV_EXCL_START
    printf(line, sqlite3_errmsg(dbh));
    close_database(dbh);
    exit(1);
  }                                                          // LCOV_EXCL_STOP
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void duplicate_to_db(sqlite3 * dbh, int count, uint64_t size, char * paths)
{
  const char * sql = "INSERT INTO duplicates (count, each_size, paths) "
                     "VALUES(?, ?, ?)";
  int rv;

  pthread_mutex_lock(&dbh_lock);

  if (stmt_duplicate_to_db == NULL) {
    rv = sqlite3_prepare_v2(dbh, sql, -1, &stmt_duplicate_to_db, NULL);
    rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);
  }

  rv = sqlite3_bind_int(stmt_duplicate_to_db, 1, count);
  rvchk(rv, SQLITE_OK, "Can't bind count: %s\n", dbh);

  rv = sqlite3_bind_int64(stmt_duplicate_to_db, 2, (sqlite3_int64)size);
  rvchk(rv, SQLITE_OK, "Can't bind file size: %s\n", dbh);

  rv = sqlite3_bind_text(stmt_duplicate_to_db, 3, paths, -1, SQLITE_STATIC);
  rvchk(rv, SQLITE_OK, "Can't bind path list: %s\n", dbh);

  rv = sqlite3_step(stmt_duplicate_to_db);
  rvchk(rv, SQLITE_DONE, "tried to add to duplicates table: %s\n", dbh);

  sqlite3_reset(stmt_duplicate_to_db);

  if (count > stats_most_dups) {
    stats_most_dups = count;
  }

  pthread_mutex_unlock(&dbh_lock);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void delete_duplicate_entry(sqlite3 * dbh, int id)
{
  const char * sql = "DELETE FROM duplicates WHERE id=?";
  int rv;

  if (stmt_delete_duplicate == NULL) {
    rv = sqlite3_prepare_v2(dbh, sql, -1, &stmt_delete_duplicate, NULL);
    rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);
  }

  rv = sqlite3_bind_int(stmt_delete_duplicate, 1, id);
  rvchk(rv, SQLITE_OK, "Can't bind id: %s\n", dbh);

  rv = sqlite3_step(stmt_delete_duplicate);
  rvchk(rv, SQLITE_DONE, "tried to delete from duplicates table: %s\n", dbh);

  sqlite3_reset(stmt_delete_duplicate);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void init_get_known_duplicates()
{
  if (known_dup_path_list != NULL) {
    return;
  }

  // Only override size for --x-small-buffers once because size gets reset
  // as needed and then we need to observe it.
  if (known_dup_path_list_first && x_small_buffers) {
    known_dup_path_list_size = 3;
    known_dup_path_list_first = 0;
  }

  known_dup_path_list =
    (char * *)calloc(known_dup_path_list_size, sizeof(char *));

  for (int i = 0; i < known_dup_path_list_size; i++) {
    known_dup_path_list[i] = (char *)malloc(DUPD_PATH_MAX);
  }
}

/** ***************************************************************************
 * Public function, see header file.
 *
 */
char * * get_known_duplicates(sqlite3  *dbh, char * path, int * dups)
{
  static char path_list[ONE_MB_BYTES];

  const char * sql = "SELECT paths FROM duplicates WHERE paths LIKE ?";
  int rv;
  int copied = 0;
  char line[DUPD_PATH_MAX];
  char * pos = NULL;
  char * token;

  LOG(L_TRACE, "get_known_duplicates(%s)\n", path);

  if (path == NULL) {                                        // LCOV_EXCL_START
    printf("error: no file specified\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if (known_dup_path_list == NULL) {                         // LCOV_EXCL_START
    printf("error: init_get_known_duplicates() not called\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if (stmt_get_known_duplicates == NULL) {
    rv = sqlite3_prepare_v2(dbh, sql, -1, &stmt_get_known_duplicates, NULL);
    rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);
  }

  snprintf(line, DUPD_PATH_MAX, "%%%s%%", path);
  rv = sqlite3_bind_text(stmt_get_known_duplicates, 1, line, -1,SQLITE_STATIC);
  rvchk(rv, SQLITE_OK, "Can't bind path list: %s\n", dbh);

  path_list[0] = 0;

  while (rv != SQLITE_DONE) {
    rv = sqlite3_step(stmt_get_known_duplicates);
    if (rv == SQLITE_DONE) { continue; }
    if (rv != SQLITE_ROW) {                                  // LCOV_EXCL_START
      printf("Error reading duplicates table!\n");
      exit(1);
    }                                                        // LCOV_EXCL_STOP

    char * p = (char *)sqlite3_column_text(stmt_get_known_duplicates, 0);
    if (strlen(p) + 1 > ONE_MB_BYTES) {                      // LCOV_EXCL_START
      printf("error: no one expects a path list this long: %zu\n", strlen(p));
      exit(1);
    }                                                        // LCOV_EXCL_STOP
    strcpy(path_list, p);

    LOG(L_TRACE, "match: %s\n", path_list);

    int separators = 0;
    for (int i = 0; path_list[i] != 0; i++) {
      if (path_list[i] == path_separator) { separators++; }
    }

    if (separators < 1) {                                    // LCOV_EXCL_START
      printf("error: db has a duplicate set with no duplicates?\n");
      printf("%s\n", path_list);
      exit(1);
    }                                                        // LCOV_EXCL_STOP

    *dups = separators;

    // known_dup_path_list is a fixed array of paths, so may need to
    // resize it if this duplicate set is too large.
    if (*dups > known_dup_path_list_size) {
      free_get_known_duplicates();
      known_dup_path_list_size = *dups + 16;
      LOG(L_RESOURCES, "Expanding known_dup_path_list_size to %d\n",
          known_dup_path_list_size);
      init_get_known_duplicates();
    }

    // The path list matched may be a false positive because we're
    // matching substrings. If the parsing loop below does not find
    // myself (i.e. 'path') then we'll have to ignore this false match
    // and keep looking in the db.
    int found_myself = 0;

    if ((token = strtok_r(path_list, path_sep_string, &pos)) != NULL) {

      if (strcmp(path, token)) {
        LOG(L_TRACE, "copying potential dup: [%s]\n", token);
        strcpy(known_dup_path_list[copied++], token);
      } else {
        found_myself = 1;
      }

      while ((token = strtok_r(NULL, path_sep_string, &pos)) != NULL) {
        if (strcmp(path, token)) {
          LOG(L_TRACE, "copying potential dup: [%s]\n", token);
          strcpy(known_dup_path_list[copied++], token);
        } else {
          found_myself = 1;
        }
      }
    }

    if (!found_myself) {
      LOG(L_TRACE, "false match, keep looking\n");
      path_list[0] = 0;
      copied = 0;
    } else {
      LOG(L_TRACE, "indeed a match for my potential duplicates\n");
      break;
    }

  } // while (rv != SQLITE_DONE)

  // If it never got around to copying anything into path_list it
  // means there was no duplicates entry containing our path.
  if (path_list[0] == 0) {
    LOG(L_TRACE, "get_known_duplicates: NONE\n");
    *dups = 0;
    sqlite3_reset(stmt_get_known_duplicates);
    return(NULL);
  }

  if (copied != *dups) {                                     // LCOV_EXCL_START
    printf("error: dups: %d  i: %d\n", *dups, copied);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  sqlite3_reset(stmt_get_known_duplicates);

  LOG_TRACE {
    printf("get_known_duplicates: dups=%d\n", *dups);
    for (int i = 0; i < *dups; i++) {
      printf("-> %s\n", known_dup_path_list[i]);
    }
  }

  return(known_dup_path_list);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void free_get_known_duplicates()
{
  if (known_dup_path_list == NULL) {
    return;
  }

  for (int i = 0; i < known_dup_path_list_size; i++) {
    free(known_dup_path_list[i]);
  }
  free(known_dup_path_list);
  known_dup_path_list = NULL;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int cache_db_find_entry(char * path, int hash_alg, uint64_t * file_id,
                        char * hash, uint64_t * size, uint32_t * timestamp)
{
  static char * sqlf = "SELECT id, size, timestamp FROM files WHERE path=?";
  static char * sqlh = "SELECT hash FROM hashes WHERE id=? AND alg=?";
  sqlite3_stmt * statement = NULL;
  uint64_t size_from_db = 0;
  uint32_t timestamp_from_db = 0;
  int found_hash = 0;
  int rv;

  *file_id = 0;

  LOG(L_FILES, "Attempting to find hash from cache for %s\n", path);

  if (*size == 0 || *timestamp == 0) {
    printf("error: cache_db_find_entry: size/timestamp can't be zero.\n");
    exit(1);
  }

  rv = sqlite3_prepare_v2(cache_dbh, sqlf, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", cache_dbh);

  rv = sqlite3_bind_text(statement, 1, path, -1, SQLITE_STATIC);
  rvchk(rv, SQLITE_OK, "Can't bind path: %s\n", cache_dbh);

  rv = sqlite3_step(statement);
  if (rv == SQLITE_ROW) {
    *file_id = (uint64_t)sqlite3_column_int64(statement, 0);
    size_from_db = (uint64_t)sqlite3_column_int64(statement, 1);
    timestamp_from_db = (uint32_t)sqlite3_column_int64(statement, 2);
  }

  sqlite3_finalize(statement);
  if (*file_id == 0) {
    LOG(L_FILES, "%s: CACHE_FILE_NOT_PRESENT\n", path);
    return CACHE_FILE_NOT_PRESENT;
  }

  // If size or timestamp don't match, hashes are invalid
  if (*timestamp != timestamp_from_db || *size != size_from_db) {
    LOG(L_MORE_TRACE, "Invalidating hashes for %s (timestamp: %" PRIu32
        " from_db: %" PRIu32 " ; size: %" PRIu64 " from_db: %" PRIu64 ")\n",
        path, *timestamp, timestamp_from_db, *size, size_from_db);
    cache_db_scrub_entry(path, *file_id, *size, *timestamp);
    LOG(L_FILES, "%s: CACHE_HASH_NOT_PRESENT\n", path);
    return CACHE_HASH_NOT_PRESENT;
  }

  LOG(L_MORE_TRACE, "cache db: file found, id=%" PRIu64 ", timestamp=%"
      PRIu32 ", size=%" PRIu64 "\n", *file_id, *timestamp, *size);

  rv = sqlite3_prepare_v2(cache_dbh, sqlh, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", cache_dbh);

  rv = sqlite3_bind_int(statement, 1, *file_id);
  rvchk(rv, SQLITE_OK, "Can't bind file_id: %s\n", cache_dbh);

  rv = sqlite3_bind_int(statement, 2, hash_alg);
  rvchk(rv, SQLITE_OK, "Can't bind hash_alg: %s\n", cache_dbh);

  rv = sqlite3_step(statement);

  if (rv == SQLITE_ROW) {
    int bytes = sqlite3_column_bytes(statement, 0);
    if (bytes != hash_bufsize) {
      printf("error: cache_db hash for alg %d has size %d, expected %d\n",
             hash_alg, bytes, hash_bufsize);
      exit(1);
    }
    memcpy(hash, sqlite3_column_blob(statement, 0), hash_bufsize);
    found_hash = 1;
  }

  sqlite3_finalize(statement);

  if (found_hash) {
    LOG(L_FILES, "%s: CACHE_HASH_FOUND\n", path);
    return CACHE_HASH_FOUND;
  } else {
    LOG(L_FILES, "%s: CACHE_HASH_NOT_PRESENT\n", path);
    return CACHE_HASH_NOT_PRESENT;
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void cache_db_add_entry(char * path, uint64_t size, uint32_t timestamp,
                        int hash_alg, char * hash, int hash_len)
{
  char hash_from_db[HASH_MAX_BUFSIZE];
  uint64_t size_from_db = size;
  uint32_t time_from_db = timestamp;
  uint64_t file_id = 0;
  int rv;

  LOG(L_FILES, "cache_db_add_entry (hash_alg=%d): %s\n", hash_alg, path);

  // If timestamp is not set, do so now.
  if (timestamp == 0) {
    STRUCT_STAT info;

    if (get_file_info(path, &info)) {
      printf("error: unable to stat %s\n", path);
      exit(1);
    }

    timestamp = (uint32_t)info.st_mtime;
    time_from_db = timestamp;
  }

  // This file may or may not be in the table already.
  // If the file has changed, size and/or timestamp may not match.
  // If it is in the table and is current, it may or may not have a hash
  // for the current hash_alg.

  // So, first let's retrieve whatever we have on this file...
  rv = cache_db_find_entry(path, hash_alg, &file_id, hash_from_db,
                           &size_from_db, &time_from_db);

  // If the hash was found then we're done, the intended hash is already there.
  // If it doesn't match, something is very wrong...

  if (rv == CACHE_HASH_FOUND) {
    if (memcmp(hash, hash_from_db, hash_len)) {
      printf("error: hash from cache db does not match hash for %s\n", path);
      memdump(" hash from db", hash_from_db, hash_len);
      memdump("computed hash", hash, hash_len);
      exit(1);
    }
    return;
  }

  // We're here, so the desired hash entry is not present.
  // The file entry may or may not. If not, let's add it first.

  if (rv == CACHE_FILE_NOT_PRESENT) {
    sqlite3_stmt * stmt1;
    const char * sqlf = "INSERT INTO files (path, size, timestamp) "
                        "VALUES (?, ?, ?)";
    rv = sqlite3_prepare_v2(cache_dbh, sqlf, -1, &stmt1, NULL);
    rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", cache_dbh);

    rv = sqlite3_bind_text(stmt1, 1, path, -1, SQLITE_STATIC);
    rvchk(rv, SQLITE_OK, "Can't bind path: %s\n", cache_dbh);

    rv = sqlite3_bind_int64(stmt1, 2, size);
    rvchk(rv, SQLITE_OK, "Can't bind size: %s\n", cache_dbh);

    rv = sqlite3_bind_int64(stmt1, 3, timestamp);
    rvchk(rv, SQLITE_OK, "Can't bind size: %s\n", cache_dbh);

    rv = sqlite3_step(stmt1);
    rvchk(rv, SQLITE_DONE, "tried to insert file: %s\n", cache_dbh);

    sqlite3_finalize(stmt1);

    // Need file_id of new file row we just added

    file_id = sqlite3_last_insert_rowid(cache_dbh);
    LOG(L_FILES, "Added file to cache db: file_id=%" PRIu64 ": %s\n",
        file_id, path);
  }

  // And then finally save the hash

  sqlite3_stmt * stmt2;
  const char * sqlh = "INSERT INTO hashes (id, alg, hash) "
                      "VALUES (?, ?, ?)";
  rv = sqlite3_prepare_v2(cache_dbh, sqlh, -1, &stmt2, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", cache_dbh);

  rv = sqlite3_bind_int(stmt2, 1, file_id);
  rvchk(rv, SQLITE_OK, "Can't bind id: %s\n", cache_dbh);

  rv = sqlite3_bind_int(stmt2, 2, hash_alg);
  rvchk(rv, SQLITE_OK, "Can't bind alg: %s\n", cache_dbh);

  rv = sqlite3_bind_blob(stmt2, 3, hash,
                         (sqlite3_uint64)hash_len, SQLITE_STATIC);
  rvchk(rv, SQLITE_OK, "Can't bind hash: %s\n", cache_dbh);

  rv = sqlite3_step(stmt2);
  rvchk(rv, SQLITE_DONE, "tried to insert hash: %s\n", cache_dbh);

  sqlite3_finalize(stmt2);
}
