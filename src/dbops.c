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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "dbops.h"
#include "main.h"
#include "stats.h"
#include "utils.h"


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

  if (verbosity >= 5) {
    printf("SQL: [%s]\n", sql);
  }

  rv = sqlite3_prepare_v2(dbh, sql, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);

  rv = sqlite3_step(statement);
  rvchk(rv, SQLITE_DONE, "Can't step: (rv=%d) %s\n", dbh);

  sqlite3_finalize(statement);
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
  if (save_uniques) {
    single_statement(dbh, "CREATE TABLE files (path TEXT)");
  }
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
    if (rv != 0) {
      char line[PATH_MAX];
      snprintf(line, PATH_MAX, "unlink %s", path);
      perror(line);
      exit(1);
    }
  }

  if (!newdb && rv == 0) {
    printf("Unable to open %s for reading...\n", path);
    exit(1);
  }

  rv = sqlite3_open(path, &dbh);
  rvchk(rv, SQLITE_OK, "Can't open database: %s\n", dbh);

  if (newdb) {
    initialize_database(dbh);
    if (verbosity >= 2) {
      printf("Done initializing new database [%s]\n", path);
    }
  }

  // Check if this db has 'files' table which will help with unique files

  if (no_unique) {
    if (verbosity >= 1) {
      printf("warning: Ignoring unique info in database!\n");
    }

  } else {
    sqlite3_stmt * statement = NULL;
    char * table_name = NULL;
    char * sql = "SELECT name FROM sqlite_master WHERE type='table'";
    rv = sqlite3_prepare_v2(dbh, sql, -1, &statement, NULL);
    rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);

    while (rv != SQLITE_DONE) {
      rv = sqlite3_step(statement);
      if (rv == SQLITE_DONE) { continue; }
      if (rv != SQLITE_ROW) {
        printf("Error reading duplicates table!\n");
        exit(1);
      }

      table_name = (char *)sqlite3_column_text(statement, 0);
      if (!strcmp(table_name, "files")) {
        have_uniques = 1;
        if (verbosity >= 3) { printf("Database has unique file info.\n"); }
      }
    }
    sqlite3_finalize(statement);
  }

  return dbh;
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
  if (rv != code) {
    printf(line, sqlite3_errmsg(dbh));
    sqlite3_close(dbh);
    exit(1);
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void duplicate_to_db(sqlite3 * dbh, int count, off_t size, char * paths)
{
  const char * sql = "INSERT INTO duplicates (count, each_size, paths) "
                     "VALUES(?, ?, ?)";
  sqlite3_stmt * statement = NULL;
  int rv;

  rv = sqlite3_prepare_v2(dbh, sql, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);

  rv = sqlite3_bind_int(statement, 1, count);
  rvchk(rv, SQLITE_OK, "Can't bind count: %s\n", dbh);

  rv = sqlite3_bind_int(statement, 2, size);
  rvchk(rv, SQLITE_OK, "Can't bind file size: %s\n", dbh);

  rv = sqlite3_bind_text(statement, 3, paths, -1, SQLITE_STATIC);
  rvchk(rv, SQLITE_OK, "Can't bind path list: %s\n", dbh);

  rv = sqlite3_step(statement);
  rvchk(rv, SQLITE_DONE, "tried to add to duplicates table: %s\n", dbh);

  sqlite3_finalize(statement);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void unique_to_db(sqlite3 * dbh, char * path, char * msg)
{
  const char * sql = "INSERT INTO files (path) VALUES (?)";
  sqlite3_stmt * statement = NULL;
  int rv;

  rv = sqlite3_prepare_v2(dbh, sql, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);

  rv = sqlite3_bind_text(statement, 1, path, -1, SQLITE_STATIC);
  rvchk(rv, SQLITE_OK, "Can't bind path list: %s\n", dbh);

  rv = sqlite3_step(statement);
  rvchk(rv, SQLITE_DONE, "tried to add to files table: %s\n", dbh);

  stats_uniques_saved++;

  sqlite3_finalize(statement);

  if (verbosity >= 4) {
    printf("Unique file (%s): [%s]\n", msg, path);
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int is_known_unique(sqlite3 * dbh, char * path)
{
  const char * sql = "SELECT path FROM files WHERE path=?";
  sqlite3_stmt * statement = NULL;
  int rv;

  if (verbosity >= 3) {
    printf("Checking files table for uniqueness [%s]\n", path);
  }

  rv = sqlite3_prepare_v2(dbh, sql, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);

  rv = sqlite3_bind_text(statement, 1, path, -1, SQLITE_STATIC);
  rvchk(rv, SQLITE_OK, "Can't bind path list: %s\n", dbh);

  char * got_path = NULL;

  while (rv != SQLITE_DONE) {
    rv = sqlite3_step(statement);
    if (rv == SQLITE_DONE) { continue; }
    if (rv != SQLITE_ROW) {
      printf("Error reading duplicates table!\n");
      exit(1);
    }

    got_path = (char *)sqlite3_column_text(statement, 0);
    if (!strcmp(got_path, path)) {
      sqlite3_finalize(statement);
      return(1);
    }
  }
  sqlite3_finalize(statement);

  return(0);
}
