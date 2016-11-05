/*
  Copyright 2012-2016 Jyri J. Virkki <jyri@virkki.com>

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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dbops.h"
#include "main.h"
#include "hash.h"
#include "report.h"
#include "scan.h"
#include "utils.h"

#define STATUS_UNKNOWN -1
#define STATUS_NOTDUP 0
#define STATUS_DUPLICATE 1
#define STATUS_EXCLUDE 2
#define STATUS_HARDLINK 3

static int print_uniques = 0;
static int print_duplicates = 0;
static int list_all_duplicates = 0;


/** ***************************************************************************
 * Prints the given path, excluding the prefix from cut_path, if applicable.
 *
 * Parameters:
 *    prefix - Printed before path. Must not be null.
 *    path   - The path to print.
 *
 * Return: none
 *
 */
static void print_path(char * prefix, char * path)
{
  if (cut_path == NULL) {
    printf("%s%s\n", prefix, path);
    return;
  }

  char * cut = strstr(path, cut_path);
  if (cut == NULL) {
    printf("%s%s\n", prefix, path);
    return;
  }

  printf("%s%s\n", prefix, path + strlen(cut_path));
}


/** ***************************************************************************
 * For the given 'path', check if that file is the same as the file in
 * 'self' which has the given 'hash'.
 *
 * Parameters:
 *    path - Compare this file against `self`.
 *    self - Path of the file to compare against.
 *    hash - Hash of the file `self`.
 *
 * Return:
 *    1 - If path and self are distinct duplicates.
 *    0 - If they are not (or cannot be tested to be) duplicates.
 *
 */
static int is_duplicate(char * path, char * self, char * hash)
{
  char hash2[16];

  if (!strcmp(path, self)) {                                 // LCOV_EXCL_START
    printf("is_duplicate: path [%s] == self [%s]\n", path, self);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if (verbosity >= 4) { printf("is_duplicate? [%s]\n", path); }

  if (!file_exists(path)) {
    if (verbosity >= 3) { printf("file no longer exists: %s\n", path); }
    return(0);
  }

  if ((*hashfn)(path, hash2, 0, hash_block_size, 0)) {
    printf("error: unable to hash %s\n", path);              // LCOV_EXCL_START
    return(0);
  }                                                          // LCOV_EXCL_STOP

  if (memcmp(hash, hash2, 16)) {
    if (verbosity >= 4) { printf("file no longer a duplicate: %s\n", path); }
    return(0);
  } else {
    if (verbosity >= 4) { printf("Yes, still a duplicate: %s\n", path); }
  }

  return(1);
}


/** ***************************************************************************
 * Given a file 'path' and a set of paths in 'duplicates', verify if those
 * are still indeed duplicates of file 'path'.
 *
 * The 'status' param points to an array of ints which must have the
 * same size as 'dups'. Each element returns the status of the corresponding
 * file in 'duplicates'. This array must be allocated by the caller, the
 * values are filled by this function. Possible values are:
 *    STATUS_UNKNOWN   - This file was not tested (only if shortcircuit)
 *    STATUS_NOTDUP    - It is not a duplicate of 'path'
 *    STATUS_DUPLICATE - It is a duplicate of 'path'
 *    STATUS_EXCLUDE   - Ignored due to exclude_path
 *    STATUS_HARDLINK  - File is a hard link of path
 *
 * Parameters:
 *    path         - Primary file to be tested against all 'duplicates'
 *    dups         - Number of paths in array 'duplicates'
 *    duplicates   - Array of paths to test against file in 'path'
 *    status       - Caller-allocated array, filled in by this function.
 *    shortcircuit - If true, return as soon as one duplicate is found.
 *
 * Return:
 *    The number of duplicates of 'path' found in 'duplicates', zero or more.
 *    Note that if 'shortcircuit' is true this can be 1 even if there were
 *    more duplicates present.
 *    If hardlink_is_unique flag is set, hard links do not count towards the
 *    total of duplicates.
 *
 */
static int reverify_duplicates(char * path, int dups, char * * duplicates,
                               int * status, int shortcircuit)
{
  char hash[16];
  STRUCT_STAT info;

  if (verbosity >= 5) {
    printf("reverify_duplicates(path=%s, dups=%d)\n", path, dups);
  }

  if (shortcircuit) {
    printf("shortcircuit not tested\n");                     // LCOV_EXCL_START
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if ((*hashfn)(path, hash, 0, hash_block_size, 0)) {
    printf("error: unable to hash %s\n", path);              // LCOV_EXCL_START
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if (get_file_info(path, &info)) {
    printf("error: unable to stat %s\n", path);
    exit(1);
  }

  ino_t path_inode = info.st_ino;

  for (int i = 0; i < dups; i++) { status[i] = STATUS_UNKNOWN; }

  int current_dups = 0;
  for (int i = 0; i < dups; i++) {

    if (exclude_path != NULL &&
        !strncmp(exclude_path, duplicates[i], exclude_path_len)) {
      status[i] = STATUS_EXCLUDE;
      continue;
    }

    if (get_file_info(duplicates[i], &info)) {
      printf("error: unable to stat %s\n", duplicates[i]);
      status[i] = STATUS_NOTDUP;
      continue;
    }

    if (info.st_ino == path_inode) {
      status[i] = STATUS_HARDLINK;
      if (!hardlink_is_unique) { current_dups++; }
      continue;
    }

    if (is_duplicate(duplicates[i], path, hash)) {
      status[i] = STATUS_DUPLICATE;
      current_dups++;
      if (shortcircuit) {
        return(current_dups);                                // LCOV_EXCL_LINE
      }

    } else {
      status[i] = STATUS_NOTDUP;
    }
  }

  if (verbosity >= 5) {
    printf("reverify_duplicates() -> %d\n", current_dups);
  }

  return(current_dups);
}


/** ***************************************************************************
 * Public function, see report.h
 *
 */
void operation_report()
{
  const char * sql = "SELECT paths, count*each_size AS total "
                     "FROM duplicates ORDER BY total";
  sqlite3_stmt * statement = NULL;
  int rv;
  char * path_list;
  char * pos = NULL;
  char * token;
  uint64_t used = 0;

  if (verbosity >= 1) {
    printf("Duplicate report from database %s:\n\n", db_path);
  }

  sqlite3 * dbh = open_database(db_path, 0);
  rv = sqlite3_prepare_v2(dbh, sql, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);

  while (rv != SQLITE_DONE) {
    rv = sqlite3_step(statement);
    if (rv == SQLITE_DONE) { continue; }
    if (rv != SQLITE_ROW) {                                  // LCOV_EXCL_START
      printf("Error reading duplicates table!\n");
      exit(1);
    }                                                        // LCOV_EXCL_STOP

    path_list = (char *)sqlite3_column_text(statement, 0);
    off_t total = sqlite3_column_int64(statement, 1);

    if (total >= minimum_file_size) {
      printf("%lu total bytes used by duplicates:\n", total);
      used += (uint64_t)total;
      if ((token = strtok_r(path_list, path_sep_string, &pos)) != NULL) {
        print_path("  ", token);
        while ((token = strtok_r(NULL, path_sep_string, &pos)) != NULL) {
          print_path("  ", token);
        }
      }

      printf("\n\n");
    }
  }

  unsigned long long kb = used / 1024;
  unsigned long mb = kb / 1024;
  unsigned long gb = mb / 1024;

  printf("Total used: %" PRIu64 " bytes (%llu KiB, %lu MiB, %lu GiB)\n",
         used, kb, mb, gb);

  sqlite3_finalize(statement);
  close_database(dbh);
}


/** ***************************************************************************
 * Callback function for walk_dir() used by various reporting operations.
 *
 * Parameters:
 *    dbh  - sqlite3 database handle.
 *    size - IGNORED
 *    path - Process this file.
 *
 * Return:
 *    0  - 'path' is a unique file (best we can tell)
 *    1+ - Number of duplicates identified.
 *
 */
static int file_callback(sqlite3 * dbh, off_t size, ino_t inode, char * path)
{
  (void)size;                   /* not used */
  (void)inode;                  /* not used */
  char * unique_pfx = "";
  char * dup_pfx = "";

  // For uniques and dups, only print the list of filenames.
  // For ls which prints both, include prefixes to identify the type.
  if (print_uniques && print_duplicates) {
    unique_pfx = "   UNIQUE: ";
    dup_pfx = "DUPLICATE: ";
  }

  // If we have a unique file table, just check that!
  if (have_uniques) {
    if (is_known_unique(dbh, path)) {
      if (print_uniques) {
        print_path(unique_pfx, path);
      }
      return(0);
    }
  }

  int dups;
  char * * dup_paths = get_known_duplicates(dbh, path, &dups);

  if (dups == 0) {
    if (print_uniques) {
      print_path(unique_pfx, path);
    }
    return(0);
  }

  // File had 1+ duplicates at scan time BUT need to verify if those
  // still really are duplicates to avoid data loss that could happen
  // if we claim this has duplicates but turns out it was the last copy.

  int shortcircuit = 0;
  int status[dups];

  int verified_dups =
    reverify_duplicates(path, dups, dup_paths, status, shortcircuit);

  int have_info = 0;

  if (verified_dups == 0 && print_uniques) {
    print_path(unique_pfx, path);
    have_info = 1;
  }

  if (verified_dups !=0 && print_duplicates) {
    print_path(dup_pfx, path);
    have_info = 1;
  }

  // Print info about each individual duplicate candidate if applicable

  if (have_info && list_all_duplicates) {
    for (int i = 0; i < dups; i++) {
      switch(status[i]) {
      case STATUS_DUPLICATE:
        print_path("             DUP: ", dup_paths[i]);
        break;
      case STATUS_HARDLINK:
        print_path("             HL : ", dup_paths[i]);
        break;
      case STATUS_EXCLUDE:
        print_path("             xxx: ", dup_paths[i]);
        break;
      case STATUS_NOTDUP:
        print_path("             ---: ", dup_paths[i]);
        break;
      default:
        printf("error: file_callback: unknown status\n");    // LCOV_EXCL_START
        exit(1);
      }                                                      // LCOV_EXCL_STOP
    }
  }

  return(verified_dups);
}


/** ***************************************************************************
 * Public function, see report.h
 *
 */
void operation_file()
{
  print_uniques = 1;
  print_duplicates = 1;
  list_all_duplicates = 1;

  sqlite3 * dbh = open_database(db_path, 0);
  init_get_known_duplicates();
  file_callback(dbh, 0, 0, file_path);
  close_database(dbh);
  free_get_known_duplicates();
}


/** ***************************************************************************
 * Public function, see report.h
 *
 */
void operation_ls()
{
  print_uniques = 1;
  print_duplicates = 1;
  if (verbosity >= 2) { list_all_duplicates = 1; }

  sqlite3 * dbh = open_database(db_path, 0);
  init_get_known_duplicates();
  init_scanlist();
  walk_dir(dbh, start_path[0], file_callback);
  close_database(dbh);
  free_get_known_duplicates();
}


/** ***************************************************************************
 * Public function, see report.h
 *
 */
void operation_uniques()
{
  print_uniques = 1;
  if (verbosity >= 2) { list_all_duplicates = 1; }

  sqlite3 * dbh = open_database(db_path, 0);
  init_get_known_duplicates();
  init_scanlist();
  walk_dir(dbh, start_path[0], file_callback);
  close_database(dbh);
  free_get_known_duplicates();
}


/** ***************************************************************************
 * Public function, see report.h
 *
 */
void operation_dups()
{
  print_duplicates = 1;
  if (verbosity >= 2) { list_all_duplicates = 1; }

  sqlite3 * dbh = open_database(db_path, 0);
  init_get_known_duplicates();
  init_scanlist();
  walk_dir(dbh, start_path[0], file_callback);
  close_database(dbh);
  free_get_known_duplicates();
}


/** ***************************************************************************
 * Public function, see report.h
 *
 */
void operation_shell_script()
{
  const char * sql = "SELECT paths FROM duplicates";
  char kept[PATH_MAX];
  sqlite3_stmt * statement = NULL;
  int rv;
  char * path_list;
  char * pos = NULL;
  char * token;

  sqlite3 * dbh = open_database(db_path, 0);
  rv = sqlite3_prepare_v2(dbh, sql, -1, &statement, NULL);
  rvchk(rv, SQLITE_OK, "Can't prepare statement: %s\n", dbh);

  printf("#\n");
  printf("# WARNING: Auto-generated by dupd to blindly delete duplicates.\n");
  printf("# Only one file in each duplicate set is kept and it might not\n");
  printf("# be the one you wanted! Review carefully before running this!\n");
  printf("#\n\n");

  while (rv != SQLITE_DONE) {
    rv = sqlite3_step(statement);
    if (rv == SQLITE_DONE) { continue; }
    if (rv != SQLITE_ROW) {                                  // LCOV_EXCL_START
      printf("Error reading duplicates table!\n");
      exit(1);
    }                                                        // LCOV_EXCL_STOP

    path_list = (char *)sqlite3_column_text(statement, 0);

    if ((token = strtok_r(path_list, path_sep_string, &pos)) != NULL) {
      printf("\n#\n# KEEPING: %s\n#\n", token);
      if (rmsh_link) {
        snprintf(kept, PATH_MAX, "%s", token);
      }
      while ((token = strtok_r(NULL, path_sep_string, &pos)) != NULL) {
        printf("rm \"%s\"\n", token);

        switch (rmsh_link) {
        case RMSH_LINK_SOFT:
          printf("ln -s \"%s\" \"%s\"\n", kept, token);
          break;
        case RMSH_LINK_HARD:
          printf("ln \"%s\" \"%s\"\n", kept, token);
          break;
        }
      }
    }
  }
  sqlite3_finalize(statement);
  close_database(dbh);
}
