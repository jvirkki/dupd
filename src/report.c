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

#include "dbops.h"
#include "main.h"
#include "md5.h"
#include "report.h"
#include "scan.h"
#include "utils.h"


/** ***************************************************************************
 * Prints the given path, excluding the prefix from cut_path, if one is
 * defined and it matches the path.
 *
 */
static void print_path(char * path)
{
  if (cut_path == NULL) {
    printf("  %s\n", path);
    return;
  }

  char * cut = strstr(path, cut_path);
  if (cut == NULL) {
    printf("  %s\n", path);
    return;
  }

  printf("  %s\n", path + strlen(cut_path));
}


/** ***************************************************************************
 * For the given 'path', check if that file is the same as the file in
 * 'self' which has the given 'hash'.
 *
 */
static int is_duplicate(char * path, char * self, char * hash, int printdup)
{
  char hash2[16];

  if (!strcmp(path, self)) {
    return(0);
  }

  if (verbosity >= 3) { printf("is_duplicate? [%s]\n", path); }

  if (!file_exists(path)) {
    if (verbosity >= 2) { printf("file no longer exists: %s\n", path); }
    return(0);
  }

  if (md5(path, hash2, 0, 0)) {
    printf("error: unable to hash %s\n", path);
    return(0);
  }

  if (memcmp(hash, hash2, 16)) {
    if (verbosity >= 3) { printf("file no longer a duplicate: %s\n", path); }
    return(0);
  }

  if (printdup) { print_path(path); }

  return(1);
}


/** ***************************************************************************
 * For the given file path, check if the database contains known duplicates
 * for it. If so, verify each listed duplicate to see if it still exists and
 * is still a duplicate. If shortcircuit is true, return as soon as one
 * duplicate is found, if any.
 *
 * Returns:
 *    -1 if the file does not exist
 *    -2 if the file cannot be read/hashed
 *    otherwise, returns the number of duplicates found (can be zero)
 *
 */
static int check_one_file(sqlite3  *dbh, char * path,
                          int printdups, int shortcircuit)
{
  const char * sql = "SELECT paths FROM duplicates WHERE paths LIKE ?";
  sqlite3_stmt * statement = NULL;
  int rv;
  char line[PATH_MAX];
  char hash[16];
  char * path_list;
  char * pos = NULL;
  char * token;

  if (path == NULL) {
    printf("error: no file specified\n");
    exit(1);
  }

  if (!file_exists(path)) {
    return(-1);
  }

  if (verbosity >= 4) { printf("Hashing %s\n", path); }

  if (md5(path, hash, 0, 0)) {
    return(-2);
  }

  rv = sqlite3_prepare_v2(dbh, sql, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);

  snprintf(line, PATH_MAX, "%%%s%%", path);
  rv = sqlite3_bind_text(statement, 1, line, -1, SQLITE_STATIC);
  rvchk(rv, SQLITE_OK, "Can't bind path list: %s\n", dbh);

  int dups = 0;

  while (rv != SQLITE_DONE) {
    rv = sqlite3_step(statement);
    if (rv == SQLITE_DONE) { continue; }
    if (rv != SQLITE_ROW) {
      printf("Error reading duplicates table!\n");
      exit(1);
    }

    path_list = (char *)sqlite3_column_text(statement, 0);

    if ((token = strtok_r(path_list, ",", &pos)) != NULL) {
      dups += is_duplicate(token, path, hash, printdups);
      if (shortcircuit && dups > 0) { goto DONE; }
      while ((token = strtok_r(NULL, ",", &pos)) != NULL) {
        dups += is_duplicate(token, path, hash, printdups);
        if (shortcircuit && dups > 0) { goto DONE; }
      }
    }
  }
  DONE:
  sqlite3_finalize(statement);
  return dups;
}


/** ***************************************************************************
 * Callback function used by walk_dir() when called by uniques().
 * Checks if the given file has any known duplicates in the database.
 *
 */
static void print_if_unique(sqlite3 * dbh, long size, char * path)
{
  int printdups = 0;
  if (verbosity >= 3) { printdups = 1; }

  // If we have a unique file list, just check that!
  if (have_uniques) {
    if (verbosity >= 5) { printf("have_uniques, check is_known_unique\n"); }
    if (is_known_unique(dbh, path)) {
      printf("%s\n", path);
      return;
    }
  }

  // Otherwise need to do some work to see if it might be unique
  int dups = check_one_file(dbh, path, printdups, 1);

  switch(dups) {
  case -1:
    printf("error: file exists but does not exist?\n%s\n", path);
    exit(1);
  case -2:
    printf("error: unable to hash %s\n", path);
    break;
  case 0:
    printf("%s\n", path);
    break;
  default:
    if (verbosity >= 4) { printf("  NOT UNIQUE: %s\n", path); }
    break;
  }
}


/** ***************************************************************************
 * Public function, see report.h
 *
 */
void report()
{
  const char * sql = "SELECT paths, count*each_size AS total "
                     "FROM duplicates ORDER BY total";
  sqlite3_stmt * statement = NULL;
  int rv;
  char * path_list;
  char * pos = NULL;
  char * token;
  unsigned long used = 0;

  if (verbosity >= 1) {
    printf("Duplicate report from database %s:\n\n", db_path);
  }

  sqlite3 * dbh = open_database(db_path, 0);
  rv = sqlite3_prepare_v2(dbh, sql, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);

  while (rv != SQLITE_DONE) {
    rv = sqlite3_step(statement);
    if (rv == SQLITE_DONE) { continue; }
    if (rv != SQLITE_ROW) {
      printf("Error reading duplicates table!\n");
      exit(1);
    }

    path_list = (char *)sqlite3_column_text(statement, 0);
    unsigned long total = sqlite3_column_int(statement, 1);

    if (verbosity >= 5) {
      printf("size (%lu) [%s]\n", total, path_list);
    }

    if (total >= minimum_report_size) {
      printf("%lu total bytes used by duplicates:\n", total);
      used += total;
      if ((token = strtok_r(path_list, ",", &pos)) != NULL) {
        print_path(token);
        while ((token = strtok_r(NULL, ",", &pos)) != NULL) {
          print_path(token);
        }
      }

      printf("\n\n");
    }
  }

  unsigned long kb = used / 1024;
  unsigned long mb = kb / 1024;
  unsigned long gb = mb / 1024;

  printf("Total used: %lu bytes (%lu KiB, %lu MiB, %lu GiB)\n", used, kb, mb, gb);

  sqlite3_finalize(statement);
}


/** ***************************************************************************
 * Public function, see report.h
 *
 */
void check_file()
{
  sqlite3 * dbh = open_database(db_path, 0);

  if (have_uniques) {
    if (is_known_unique(dbh, file_path)) {
      printf("It is unique according to unique info saved during scan.\n");
      sqlite3_close(dbh);
      exit(0);
    }
  }

  if (verbosity >= 1) {
    printf("Duplicates for %s:\n", file_path);
  }

  int dups = check_one_file(dbh, file_path, 1, 0);
  sqlite3_close(dbh);

  switch(dups) {
  case -1:
    printf("error: not such file: %s\n", file_path);
    exit(1);
  case -2:
    printf("error: unable to hash %s\n", file_path);
    exit(1);
  default:
    if (verbosity > 0) {
      printf("It has %d known duplicates in the database.\n", dups);
    }
  }
}


/** ***************************************************************************
 * Public function, see report.h
 *
 */
void uniques()
{
  if (start_path[0] == NULL) {
    printf("error: no --path given\n");
    exit(1);
  }

  if (verbosity > 0) {
    printf("Looking for unique files in %s\n", start_path[0]);
  }

  sqlite3 * dbh = open_database(db_path, 0);
  walk_dir(dbh, start_path[0], print_if_unique);
  sqlite3_close(dbh);
}
