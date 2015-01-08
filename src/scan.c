/*
  Copyright 2012-2015 Jyri J. Virkki <jyri@virkki.com>

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
#include "md5.h"
#include "paths.h"
#include "scan.h"
#include "sizelist.h"
#include "sizetree.h"
#include "stats.h"
#include "utils.h"


/** ***************************************************************************
 * Public function, see scan.h
 *
 */
void walk_dir(sqlite3 * dbh, const char * path,
              int (*process_file)(sqlite3 *, long, char *))
{
  int rv;

  if (path == NULL || path[0] == 0) {
    printf("walk_dir called on null or empty path!\n");
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

  char newpath[PATH_MAX];
  struct dirent * entry;
  STRUCT_STAT new_stat_info;

  while ((entry = readdir(dir))) {
    if (!strncmp(".", entry->d_name, 1) || !strncmp("..", entry->d_name, 2)) {
      continue;
    }

    snprintf(newpath, PATH_MAX, "%s/%s", path, entry->d_name);

    // Skip files with a comma in them because dupd uses comma as a separator
    // in the sqlite duplicates table. It shouldn't, but for now just skip
    // these files to avoid confusion.

    if (strchr(newpath, ',')) {
      if (verbosity >= 1) {
        printf("SKIP (comma) [%s]\n", newpath);
      }
      continue;
    }

    rv = get_file_info(newpath, &new_stat_info);

    if (rv == 0) {
      if (S_ISDIR(new_stat_info.st_mode)) {
        walk_dir(dbh, newpath, process_file);
        continue;
      }

      if (S_ISREG(new_stat_info.st_mode)) {
        if (verbosity >= 4) {
          printf("FILE: [%s]\n", newpath);
        }

        if (new_stat_info.st_size > 0) {
          (*process_file)(dbh, new_stat_info.st_size, newpath);
          stats_total_bytes += new_stat_info.st_size;
          stats_files_count++;
          stats_avg_file_size = stats_avg_file_size +
            ((new_stat_info.st_size - stats_avg_file_size)/stats_files_count);

          if (verbosity >= 2) {
            if ((stats_files_count % 5000) == 0) {
              printf("Files scanned: %ld\n", stats_files_count);
            }
          }
        } else {
          if (verbosity >= 4) {
            printf("SKIP (zero size): [%s]\n", newpath);
          }
        }

      } else { // if not regular file
        if (verbosity >= 4) {
          printf("SKIP (not file) [%s]\n", newpath);
        }
        stats_files_ignored++;
      }
    } else { // if error from stat
      if (verbosity >= 1) {
        printf("SKIP (error) [%s]\n", newpath);
      }
      stats_files_error++;
    }
  }

  closedir(dir);
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

  if (write_db) {
    dbh = open_database(db_path, 1);
    begin_transaction(dbh);
  }

  // Scan phase - stat all files and build size tree, size list and path list

  long t1 = get_current_time_millis();
  for (int i=0; start_path[i] != NULL; i++) {
    walk_dir(dbh, start_path[i], add_file);
  }

  if (verbosity >= 1) {
    printf("Files scanned: %ld\n", stats_files_count);
  }

  if (verbosity >= 2) {
    long t2 = get_current_time_millis();
    stats_time_scan = t2 - t1;
    printf("Average file size: %ld\n", stats_avg_file_size);
    printf("Special files ignored: %d\n", stats_files_ignored);
    printf("Files with stat errors: %d\n", stats_files_error);
    printf("File scan completed in %ldms\n", stats_time_scan);
    report_size_list();
  }

  if (save_uniques) {
    if (verbosity >= 3) {
      printf("Saving files with unique sizes from size tree...\n");
    }
    find_unique_sizes(dbh);
  }

  // Processing phase - walk through size list whittling down the potentials

  t1 = get_current_time_millis();
  process_size_list(dbh);
  stats_time_process = get_current_time_millis() - t1;
  if (verbosity >= 2) {
    printf("Duplicate processing completed in %ldms\n", stats_time_process);
  }

  if (write_db) {
    commit_transaction(dbh);
    close_database(dbh);
  }

  if (verbosity >= 3) {
    report_path_block_usage();
  }

  report_stats();
}
