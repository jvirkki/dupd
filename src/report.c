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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dbops.h"
#include "main.h"
#include "report.h"
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
      if ((token = strtok_r(path_list, ",", &pos)) != NULL) {
        print_path(token);
        while ((token = strtok_r(NULL, ",", &pos)) != NULL) {
          print_path(token);
        }
      }

      printf("\n\n");
    }
  }
  sqlite3_finalize(statement);
}
