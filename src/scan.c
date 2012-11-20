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

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "dbops.h"
#include "filecompare.h"
#include "hashlist.h"
#include "main.h"
#include "paths.h"
#include "scan.h"
#include "sizelist.h"
#include "sizetree.h"
#include "stats.h"
#include "utils.h"

static int files_count;         // total files processed so far
static int softlinks;           // count of soft links ignored
static long avg_size;           // average file size


/** ***************************************************************************
 * Walk down the directory path given and process each file found.
 *
 * Parameters:
 *    path - The path to process. Must not be null or empty.
 *
 * Return: none
 *
 */
static void walk_dir(const char * path)
{
  int rv;

  if (path == NULL || path[0] == 0) {
    printf("walk_dir called on null or empty path!");
    exit(1);
  }

  if (verbosity >= 4) {
    printf("\nDIR: [%s]\n", path);
  }

  DIR * dir = opendir(path);
  if (dir == NULL) {
    if (verbosity >= 3) {
      perror(path);
    }
    return;
  }

  char * newpath = (char *)malloc(PATH_MAX);
  struct dirent * entry;
  STRUCT_STAT new_stat_info;

  while ((entry = readdir(dir))) {
    if (!strncmp(".", entry->d_name, 1) || !strncmp("..", entry->d_name, 2)) {
      continue;
    }

    snprintf(newpath, PATH_MAX, "%s/%s", path, entry->d_name);
    rv = get_file_info(newpath, &new_stat_info);

    if (rv == 0) {
      if (S_ISDIR(new_stat_info.st_mode)) {
        walk_dir(newpath);

      } else {
        if (verbosity >= 4) {
          printf("FILE: [%s]\n", newpath);
        }

        // Don't follow symlinks
        if (S_ISLNK(new_stat_info.st_mode)) {
          softlinks++;
          if (verbosity >= 4) {
            printf("Ignoring soft link [%s]\n", newpath);
          }
        } else {
          add_file(new_stat_info.st_size, newpath);
        }

        files_count++;
        avg_size = avg_size + ((new_stat_info.st_size - avg_size)/files_count);

        if (verbosity >= 2) {
          if ((files_count % 5000) == 0) {
            printf("Files scanned: %d\n", files_count);
          }
        }
      }
    } else {
      if (verbosity >= 1) {
        printf("SKIP (error) [%s]\n", newpath);
      }
    }
  }

  closedir(dir);
  free(newpath);
}


/** ***************************************************************************
 * Public function, see scan.h
 *
 */
void scan()
{
  sqlite3 * dbh = NULL;

  init_size_list();
  init_path_block();
  init_hash_lists();
  init_filecompare();

  // Scan phase - stat all files and build size tree, size list and path list

  long t1 = get_current_time_millis();
  walk_dir(start_path);

  if (verbosity >= 1) {
    printf("Files scanned: %d\n", files_count);
  }

  if (verbosity >= 2) {
    long t2 = get_current_time_millis();
    printf("Average file size: %ld\n", avg_size);
    printf("Soft links ignored: %d\n", softlinks);
    printf("File scan completed in %ldms\n", t2 - t1);
    report_size_list();
  }

  if (write_db) {
    dbh = open_database(db_path, 1);
    begin_transaction(dbh);
  }

  // Processing phase - walk through size list whittling down the potentials

  t1 = get_current_time_millis();
  process_size_list(dbh);
  if (verbosity >= 2) {
    long t2 = get_current_time_millis();
    printf("Duplicate processing completed in %ldms\n", t2 - t1);
  }

  if (write_db) {
    commit_transaction(dbh);
    sqlite3_close(dbh);
  }

  if (verbosity >= 3) {
    report_path_block_usage();
  }

  report_stats();
}
