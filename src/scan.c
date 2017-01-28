/*
  Copyright 2012-2017 Jyri J. Virkki <jyri@virkki.com>

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
#include "readlist.h"
#include "scan.h"
#include "sizelist.h"
#include "sizetree.h"
#include "stats.h"
#include "utils.h"


#define D_DIR 1
#define D_FILE 2
#define D_OTHER 3
#define D_ERROR 4

static char * scan_list = NULL;
static int scan_list_capacity = 0;
static int scan_list_pos = -1;


/** ***************************************************************************
 * Public function, see scan.h
 *
 */
void init_scanlist()
{
  scan_list_capacity = x_small_buffers ? 1 : 1000;
  scan_list = (char *)malloc(PATH_MAX * scan_list_capacity);
  scan_list_pos = -1;
}


/** ***************************************************************************
 * Public function, see scan.h
 *
 */
void free_scanlist()
{
  scan_list_capacity = 0;
  scan_list_pos = -1;
  if (scan_list) {
    free(scan_list);
  }
}


/** ***************************************************************************
 * Public function, see scan.h
 *
 */
void walk_dir(sqlite3 * dbh, const char * path,
              int (*process_file)(sqlite3 *, dev_t, ino_t, off_t, char *))
{
  STRUCT_STAT new_stat_info;
  int rv;

  struct dirent * entry;
  char newpath[PATH_MAX];
  char current[PATH_MAX];

  dev_t device;
  ino_t inode;
  off_t size;
  long type;

  if (path == NULL || path[0] == 0) {                        // LCOV_EXCL_START
    printf("walk_dir called on null or empty path!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  strcpy(scan_list, path);
  scan_list_pos = 0;

  while (scan_list_pos >= 0) {

    strcpy(current, scan_list + PATH_MAX * scan_list_pos);
    if (verbosity >= 6) {
      printf("\nDIR: (%d)[%s]\n", scan_list_pos, current);
    }
    scan_list_pos--;

    DIR * dir = opendir(current);
    if (dir == NULL) {                                       // LCOV_EXCL_START
      if (verbosity >= 3) {
        perror(current);
      }
      continue;
    }                                                        // LCOV_EXCL_STOP

    while ((entry = readdir(dir))) {

      char first = entry->d_name[0];
      if (!scan_hidden && first == '.') {
        continue;
      }

      if (first == '.') {
        if (entry->d_name[1] == 0) { continue; }
        if (entry->d_name[1] == '.' && entry->d_name[2] == 0) { continue; }
      }

      // Skip files with 'path_separator' in them because dupd uses this
      // character as a separator in the sqlite duplicates table.

      if (strchr(entry->d_name, path_separator)) {
        if (verbosity >= 1) {
          printf("SKIP (due to %c) [%s/%s]\n",
                 path_separator, current, entry->d_name);
        }
        continue;
      }

      snprintf(newpath, PATH_MAX, "%s/%s", current, entry->d_name);

      // If DIRENT_HAS_TYPE, we can get the type of the file from entry->d_type
      // which means we can skip doing stat() on it here and instead let
      // the worker thread do it. That means we won't know the size of the file
      // yet but that's ok. If so, set it to SCAN_SIZE_UNKNOWN to let the
      // worker thread know it'll have to get the size.
      // This doesn't work on some filesystems such as XFS. In that case, fall
      // back on calling stat() as usual.

      type = D_OTHER;

#ifdef DIRENT_HAS_TYPE
      size = SCAN_SIZE_UNKNOWN;
      inode = SCAN_INODE_UNKNOWN;
      device = SCAN_DEV_UNKNOWN;
      if (entry->d_type == DT_REG) {
        type = D_FILE;
      } else if (entry->d_type == DT_DIR) {
        type = D_DIR;
      }
#endif
      if (type == D_OTHER) {
        rv = get_file_info(newpath, &new_stat_info);
        size = new_stat_info.st_size;
        inode = new_stat_info.st_ino;
        device = new_stat_info.st_dev;
        if (rv != 0) {
          type = D_ERROR;
        } else if (S_ISDIR(new_stat_info.st_mode)) {
          type = D_DIR;
        } else if (S_ISREG(new_stat_info.st_mode)) {
          type = D_FILE;
        }
      }

      switch(type) {

      case D_DIR:
        // If 'newpath' is a directory, save it in scan_list for later
        scan_list_pos++;
        if (scan_list_pos > scan_list_usage_max) {
          scan_list_usage_max = scan_list_pos;
        }
        if (scan_list_pos == scan_list_capacity) {
          scan_list_resizes++;
          scan_list_capacity *= 2;
          scan_list =
            (char *)realloc(scan_list, PATH_MAX * scan_list_capacity);
          if (verbosity >= 5) {
            printf("Had to increase scan_list_capacity to %d\n",
                   scan_list_capacity);
          }
        }
        strcpy(scan_list + PATH_MAX * scan_list_pos, newpath);
        if (verbosity >= 8) {
          printf("queued dir at %d: %s\n", scan_list_pos, newpath);
        }
        break;

      case D_FILE:
        // If it is a file, just process it now
        (*process_file)(dbh, device, inode, size, newpath);
        break;

      case D_OTHER:
        if (verbosity >= 4) {
          printf("SKIP (not file) [%s]\n", newpath);
        }
        stats_files_ignored++;
        break;

      case D_ERROR:
        if (verbosity >= 1) {                                // LCOV_EXCL_START
          printf("SKIP (error) [%s]\n", newpath);
        }
        stats_files_error++;
        break;
      }                                                      // LCOV_EXCL_STOP
    }
    closedir(dir);
  }
}


/** ***************************************************************************
 * Public function, see scan.h
 *
 */
void scan()
{
  sqlite3 * dbh = NULL;
  STRUCT_STAT stat_info;

  init_size_list();
  init_path_block();
  init_hash_lists();
  init_filecompare();
  init_sizetree();
  init_scanlist();

  if (hdd_mode) { init_read_list(); }

  if (write_db) {
    dbh = open_database(db_path, 1);
    begin_transaction(dbh);
  }

  // Scan phase - stat all files and build size tree, size list and path list

  long t1 = get_current_time_millis();
  for (int i=0; start_path[i] != NULL; i++) {
    int rv = get_file_info(start_path[i], &stat_info);
    if (rv != 0) {
      printf("error: skipping requested path [%s]\n", start_path[i]);
    } else {
      if (threaded_sizetree) {
        walk_dir(dbh, start_path[i], add_queue);
      } else {
        walk_dir(dbh, start_path[i], add_file);
      }
    }
  }
  long t2 = get_current_time_millis();
  stats_time_scan = t2 - t1;

  if (threaded_sizetree) {
    scan_done();
  }

  if (verbosity >= 1) {
    printf("Files scanned: %" PRIu32 " (%ldms)\n",
           stats_files_count, stats_time_scan);
  }

  if (stats_files_count == 0) {
    if (write_db) {
      commit_transaction(dbh);
      close_database(dbh);
    }
    return;
  }

  if (verbosity >= 2) {
    printf("Average file size: %ld\n", stats_avg_file_size);
    printf("Special files ignored: %d\n", stats_files_ignored);
    printf("Files with stat errors: %d\n", stats_files_error);
    printf("File scan completed in %ldms\n", stats_time_scan);
    report_size_list();
    printf("Longest path list %d (file size: %ld)\n",
           stats_max_pathlist, stats_max_pathlist_size);
  }

  if (stats_max_pathlist > stats_files_count) {
                                                             // LCOV_EXCL_START
    printf("error: longest path list is larger than total files scanned!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if (save_uniques) {
    if (verbosity >= 3) {
      printf("Saving files with unique sizes from size tree...\n");
    }
    find_unique_sizes(dbh);
  }

  if (hdd_mode) {
    t1 = get_current_time_millis();
    sort_read_list();
    if (verbosity >= 2) {
      long sort_time = get_current_time_millis() - t1;
      printf("Time to sort read list: %ldms\n", sort_time);
    }
  }

  // Processing phase - walk through size list whittling down the potentials

  t1 = get_current_time_millis();

  if (x_analyze) {
    analyze_process_size_list(dbh);
  } else {
    if (hdd_mode) {
      threaded_process_size_list_hdd(dbh);
    } else {
      if (threaded_hashcompare) {
        threaded_process_size_list(dbh);
      } else {
        process_size_list(dbh);
      }
    }
  }

  stats_time_process = get_current_time_millis() - t1;

  if (verbosity >= 1) {
    printf("Duplicate processing completed in %ldms\n", stats_time_process);
  }

  if (verbosity >= 2) {
    printf("Largest duplicate set %d\n", stats_most_dups);
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
