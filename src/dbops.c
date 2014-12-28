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

#define ONE_MB_BYTES 1048576

static char * * known_dup_path_list = NULL;
static sqlite3_stmt * stmt_is_known_unique = NULL;
static sqlite3_stmt * stmt_duplicate_to_db = NULL;
static sqlite3_stmt * stmt_unique_to_db = NULL;
static sqlite3_stmt * stmt_get_known_duplicates = NULL;


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
void close_database(sqlite3 * dbh)
{
  if (dbh == NULL) {
    if (verbosity >= 4) {
      printf("warning: ignoring attempt to close NULL database\n");
    }
    return;
  }

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

  if (stmt_get_known_duplicates != NULL) {
    sqlite3_finalize(stmt_get_known_duplicates);
  }

  int rv = sqlite3_close(dbh);
  if (rv == SQLITE_OK) {
    if (verbosity >= 5) {
      printf("closed database\n");
    }
    return;
  }

  if (verbosity >= 1) {
    printf("warning: unable to close database!\n");
  }
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
    close_database(dbh);
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
  int rv;

  if (stmt_duplicate_to_db == NULL) {
    rv = sqlite3_prepare_v2(dbh, sql, -1, &stmt_duplicate_to_db, NULL);
    rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);
  }

  rv = sqlite3_bind_int(stmt_duplicate_to_db, 1, count);
  rvchk(rv, SQLITE_OK, "Can't bind count: %s\n", dbh);

  rv = sqlite3_bind_int(stmt_duplicate_to_db, 2, size);
  rvchk(rv, SQLITE_OK, "Can't bind file size: %s\n", dbh);

  rv = sqlite3_bind_text(stmt_duplicate_to_db, 3, paths, -1, SQLITE_STATIC);
  rvchk(rv, SQLITE_OK, "Can't bind path list: %s\n", dbh);

  rv = sqlite3_step(stmt_duplicate_to_db);
  rvchk(rv, SQLITE_DONE, "tried to add to duplicates table: %s\n", dbh);

  sqlite3_reset(stmt_duplicate_to_db);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void unique_to_db(sqlite3 * dbh, char * path, char * msg)
{
  const char * sql = "INSERT INTO files (path) VALUES (?)";
  int rv;

  if (stmt_unique_to_db == NULL) {
    rv = sqlite3_prepare_v2(dbh, sql, -1, &stmt_unique_to_db, NULL);
    rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);
  }

  rv = sqlite3_bind_text(stmt_unique_to_db, 1, path, -1, SQLITE_STATIC);
  rvchk(rv, SQLITE_OK, "Can't bind path list: %s\n", dbh);

  rv = sqlite3_step(stmt_unique_to_db);
  rvchk(rv, SQLITE_DONE, "tried to add to files table: %s\n", dbh);

  stats_uniques_saved++;

  sqlite3_reset(stmt_unique_to_db);

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
  int rv;

  if (verbosity >= 3) {
    printf("Checking files table for uniqueness [%s]\n", path);
  }

  if (stmt_is_known_unique == NULL) {
    rv = sqlite3_prepare_v2(dbh, sql, -1, &stmt_is_known_unique, NULL);
    rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);
  }

  rv = sqlite3_bind_text(stmt_is_known_unique, 1, path, -1, SQLITE_STATIC);
  rvchk(rv, SQLITE_OK, "Can't bind path list: %s\n", dbh);

  char * got_path = NULL;

  while (rv != SQLITE_DONE) {
    rv = sqlite3_step(stmt_is_known_unique);
    if (rv == SQLITE_DONE) { continue; }
    if (rv != SQLITE_ROW) {
      printf("Error reading duplicates table!\n");
      exit(1);
    }

    got_path = (char *)sqlite3_column_text(stmt_is_known_unique, 0);
    if (!strcmp(got_path, path)) {
      sqlite3_reset(stmt_is_known_unique);
      if (verbosity >= 5) {
	printf("is present in uniques table: %s\n", path);
      }
      return(1);
    }
  }
  sqlite3_reset(stmt_is_known_unique);

  return(0);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void init_get_known_duplicates()
{
  if (verbosity >= 5) {
    printf("init_get_known_duplicates()\n");
  }

  if (known_dup_path_list != NULL) {
    return;
  }

  known_dup_path_list = (char * *)calloc(MAX_DUPLICATES, sizeof(char *));
  for (int i = 0; i < MAX_DUPLICATES; i++) {
    known_dup_path_list[i] = (char *)malloc(PATH_MAX);
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
  char line[PATH_MAX];
  char * pos = NULL;
  char * token;

  if (verbosity >= 5) {
    printf("get_known_duplicates(%s)\n", path);
  }

  if (path == NULL) {
    printf("error: no file specified\n");
    exit(1);
  }

  if (known_dup_path_list == NULL) {
    printf("error: init_get_known_duplicates() not called\n");
    exit(1);
  }

  if (stmt_get_known_duplicates == NULL) {
    rv = sqlite3_prepare_v2(dbh, sql, -1, &stmt_get_known_duplicates, NULL);
    rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);
  }

  snprintf(line, PATH_MAX, "%%%s%%", path);
  rv = sqlite3_bind_text(stmt_get_known_duplicates, 1, line, -1,SQLITE_STATIC);
  rvchk(rv, SQLITE_OK, "Can't bind path list: %s\n", dbh);

  path_list[0] = 0;

  while (rv != SQLITE_DONE) {
    rv = sqlite3_step(stmt_get_known_duplicates);
    if (rv == SQLITE_DONE) { continue; }
    if (rv != SQLITE_ROW) {
      printf("Error reading duplicates table!\n");
      exit(1);
    }

    char * p = (char *)sqlite3_column_text(stmt_get_known_duplicates, 0);
    if (strlen(p) + 1 > ONE_MB_BYTES) {
      printf("error: no one expects a path list this long: %zu\n", strlen(p));
      exit(1);
    }
    strcpy(path_list, p);

    if (verbosity >= 5) {
      printf("match: %s\n", path_list);
    }

    int commas = 0;
    for (int i = 0; path_list[i] != 0; i++) {
      if (path_list[i] == ',') { commas++; }
    }

    if (commas < 1) {
      printf("error: db has a duplicate set with no duplicates?\n");
      printf("%s\n", path_list);
      exit(1);
    }

    *dups = commas;

    if (*dups > MAX_DUPLICATES) {
      printf("error: never expected to see %d duplicates (max=%d)\n",
	     *dups, MAX_DUPLICATES);
      exit(1);
    }

    // The path list matched may be a false positive because we're
    // matching substrings. If the parsing loop below does not find
    // myself (i.e. 'path') then we'll have to ignore this false match
    // and keep looking in the db.
    int found_myself = 0;

    if ((token = strtok_r(path_list, ",", &pos)) != NULL) {

      if (strcmp(path, token)) {
	if (verbosity >= 5) {
	  printf("copying potential dup: [%s]\n", token);
	}
	strcpy(known_dup_path_list[copied++], token);
      } else {
	found_myself = 1;
      }

      while ((token = strtok_r(NULL, ",", &pos)) != NULL) {
	if (strcmp(path, token)) {
	  if (verbosity >= 5) {
	    printf("copying potential dup: [%s]\n", token);
	  }
	  strcpy(known_dup_path_list[copied++], token);
	} else {
	  found_myself = 1;
	}
      }
    }

    if (!found_myself) {
      if (verbosity >= 5) {
	printf("false match, keep looking\n");
      }
      path_list[0] = 0;
      copied = 0;
    } else {
      if (verbosity >= 5) {
	printf("indeed a match for my potential duplicates\n");
      }
      break;
    }

  } // while (rv != SQLITE_DONE)

  // If it never got around to copying anything into path_list it
  // means there was no duplicates entry containing our path.
  if (path_list[0] == 0) {
    if (verbosity >= 5) {
      printf("get_known_duplicates: NONE\n");
    }
    *dups = 0;
    sqlite3_reset(stmt_get_known_duplicates);
    return(NULL);
  }

  if (copied != *dups) {
    printf("error: dups: %d  i: %d\n", *dups, copied);
    exit(1);
  }

  sqlite3_reset(stmt_get_known_duplicates);

  if (*dups > 0) {
    if (verbosity >= 5) {
      printf("get_known_duplicates: dups=%d\n", *dups);
      for (int i = 0; i < *dups; i++) {
        printf("-> %s\n", known_dup_path_list[i]);
      }
    }
    return(known_dup_path_list);

  } else {
    printf("get_known_duplicates: dups=0\n");
    return(NULL);
  }
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

  for (int i = 0; i < MAX_DUPLICATES; i++) {
    free(known_dup_path_list[i]);
  }
  free(known_dup_path_list);
}
