/*
  Copyright 2016-2018 Jyri J. Virkki <jyri@virkki.com>

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
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "dbops.h"
#include "main.h"
#include "refresh.h"
#include "utils.h"


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void operation_refresh()
{
  const char * sql = "SELECT id, count, each_size, paths FROM duplicates";
  sqlite3_stmt * statement = NULL;
  int rv;

  int entry_id;
  int entry_count;
  uint64_t entry_each_size;
  char * entry_path_list;
  char * pos;

  int new_list_size = DUPD_PATH_MAX;
  int new_entry_count;
  int new_pos;
  char * token;
  char * original = NULL;

  LOG(L_BASE, "Refreshing database %s:\n\n", db_path);

  char * new_list = (char *)malloc(new_list_size + 1);

  LOG_PROGRESS {
    original = (char *)malloc(new_list_size + 1);
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

    entry_id = sqlite3_column_int(statement, 0);
    entry_count = sqlite3_column_int(statement, 1);
    entry_each_size = sqlite3_column_int(statement, 2);
    entry_path_list = (char *)sqlite3_column_text(statement, 3);

    int entry_path_list_len = strlen(entry_path_list);
    if (entry_path_list_len > new_list_size) {
      new_list_size = entry_path_list_len * 2;
      new_list = (char *)realloc(new_list, new_list_size);
      if (original != NULL) {
        original = (char *)realloc(original, new_list_size);
      }
      LOG(L_RESOURCES,
          "Had to increase new_list capacity to %d\n", new_list_size);
    }

    new_entry_count = 0;
    new_pos = 0;
    new_list[0] = 0;

    if (original != NULL) {
      strcpy(original, entry_path_list);
    }

    if ((token = strtok_r(entry_path_list, path_sep_string, &pos)) != NULL) {
      if (file_exists(token)) {
        sprintf(new_list, "%s%s", token, path_sep_string);
        new_pos = strlen(token) + 1;
        new_entry_count++;
      }

      while ((token = strtok_r(NULL, path_sep_string, &pos)) != NULL) {
        if (file_exists(token)) {
          sprintf(new_list + new_pos, "%s%s", token, path_sep_string);
          new_pos += strlen(token) + 1;
          new_entry_count++;
        }
      }
    } else {                                                 // LCOV_EXCL_START
      printf("error: db has a duplicate set with no duplicates?\n");
      printf("%s\n", entry_path_list);
      exit(1);
    }                                                        // LCOV_EXCL_STOP

    if (new_pos > 0) {
      new_list[new_pos - 1] = 0; // remove final path_sep
    }

    if (new_entry_count != entry_count) {

      LOG_PROGRESS {
        printf("FROM: %s\n", original);
        printf("  TO: %s\n\n", new_list);
      }

      delete_duplicate_entry(dbh, entry_id);
      if (new_entry_count > 1) {
        duplicate_to_db(dbh, new_entry_count, entry_each_size, new_list);
      }
    }
  }

  sqlite3_finalize(statement);
  close_database(dbh);
  free(new_list);
  if (original != NULL) {
    free(original);
  }
}
