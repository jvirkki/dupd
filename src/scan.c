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

#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "dbops.h"
#include "dirtree.h"
#include "filecompare.h"
#include "main.h"
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

struct scan_list_entry {
  struct direntry * dir_entry;
  char path[DUPD_PATH_MAX];
};

static struct scan_list_entry * scan_list = NULL;
static int scan_list_capacity = 0;
static int scan_list_pos = -1;
static int scan_completed = 0;
static long scan_phase_started;
static long read_phase_started;
static int fiemap_ok = 1;
pthread_mutex_t status_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t status_cond = PTHREAD_COND_INITIALIZER;

#define SHOW_LINE printf("%s", line); fflush(stdout);


/** ***************************************************************************
 * Sleep a bit while updating status.
 *
 */
static void status_wait()
{
  struct timespec timeout;
  struct timeval now;

  gettimeofday(&now, NULL);

  timeout.tv_sec = now.tv_sec;
  timeout.tv_nsec = (now.tv_usec + 1000UL * 250) * 1000UL;
  if (timeout.tv_nsec > 1000000000L) {
    timeout.tv_nsec -= 1000000000L;
    timeout.tv_sec++;
  }
  pthread_mutex_lock(&status_lock);
  pthread_cond_timedwait(&status_cond, &status_lock, &timeout);
  pthread_mutex_unlock(&status_lock);
}


/** ***************************************************************************
 * Print out scan progress stats while scan is ongoing.
 *
 */
static void * scan_status(void * arg)
{
  (void)arg;
  char line[100];
  char timebuf[20];
  int c = 0;
  long delta;
  long kread;
  long ksec;
  const char * files =
    "Files: %8u                      %6u errors                 %12s";
  const char * sets =
    "Sets : %8u/%8u %10uK (%7ldK/s) %4uq %3d%%%c      %12s";
  const char * sets_done =
    "Round %d: %8u groups of duplicates confirmed                   %12s\n";

  // Loop showing count of files being scanned until that phase is done
  while (stats_time_scan == -1) {
    printf("\033[%dD", c);
    delta = get_current_time_millis() - scan_phase_started;
    time_string(timebuf, 20, delta);
    c = snprintf(line, 100, files,
                 (unsigned int)s_total_files_seen,
                 (unsigned int)stats_files_error,
                 timebuf);
    SHOW_LINE;
    status_wait();
  }

  // Show final count along with time this phase took
  pthread_mutex_lock(&status_lock);
  printf("\033[%dD", c);
  time_string(timebuf, 20, stats_time_scan);
  c = snprintf(line, 100, files,
               (unsigned int)s_total_files_seen,
               (unsigned int)stats_files_error,
               timebuf);
  SHOW_LINE;
  printf("\n");
  pthread_mutex_unlock(&status_lock);

  int round = 0;
  int queued;
  int bfpct;
  char scantype = 'b';

  do {
    do {
      printf("\033[%dD", c);
      delta = get_current_time_millis() - read_phase_started;
      time_string(timebuf, 20, delta);
      delta = delta / 1000;
      kread = stats_total_bytes_read / 1024;
      ksec = delta == 0 ? 0 : kread / delta;
      queued = 0;
      for (int q = 0; q < MAX_HASHER_THREADS; q++) {
        queued += stats_hasher_queue_len[q];
      }
      if (queued < 0) { queued = 0; }
      bfpct = (int)(100 * stats_read_buffers_allocated / buffer_limit);
      if (bfpct > 999) { bfpct = 999; } // Keep alignment if this spikes
      if (stats_flusher_active) { scantype = 'B'; } else { scantype = 'b'; }
      c = snprintf(line, 100, sets, stats_size_list_done,
                   s_stats_size_list_count, kread, ksec, queued,
                   bfpct, scantype, timebuf);
      SHOW_LINE;
      status_wait();

    } while (stats_round_duration[round] < 0);

    printf("\033[%dD", c);
    time_string(timebuf, 20, stats_round_duration[round]);

    c = snprintf(line, 100, sets_done, round + 1,
                 stats_duplicate_groups[round], timebuf);
    SHOW_LINE;

  } while (++round < ROUNDS);

  return NULL;
}


/** ***************************************************************************
 * Public function, see scan.h
 *
 */
void init_scanlist()
{
  scan_list_capacity = x_small_buffers ? 1 : 16;
  scan_list = (struct scan_list_entry *)malloc(scan_list_capacity *
                                               sizeof(struct scan_list_entry));
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
void walk_dir(sqlite3 * dbh, const char * path, struct direntry * dir_entry,
              dev_t device,
              int (*process_file)(sqlite3 *, ino_t, uint64_t, char *,
                                  char *, struct direntry *))
{
  STRUCT_STAT new_stat_info;
  int rv;
  int curlen;
  struct dirent * entry;
  char newpath[DUPD_PATH_MAX];
  struct direntry * current_dir_entry;
  char current[DUPD_PATH_MAX];
  ino_t inode;
  uint64_t size;
  long type;

  if (path == NULL || path[0] == 0) {                        // LCOV_EXCL_START
    printf("walk_dir called on null or empty path!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  // Initialize scan_list with the top level starting dir
  scan_list_pos = 0;
  scan_list[scan_list_pos].dir_entry = dir_entry;
  strlcpy(scan_list[scan_list_pos].path, path, DUPD_PATH_MAX);

  // Process directories off the scan_list until none left
  while (scan_list_pos >= 0) {

    curlen = strlen(scan_list[scan_list_pos].path);
    strlcpy(current, scan_list[scan_list_pos].path, DUPD_PATH_MAX);
    current_dir_entry = scan_list[scan_list_pos].dir_entry;

    LOG(L_FILES, "\nDIR: (%d)[%s]\n", scan_list_pos, current);
    scan_list_pos--;

    DIR * dir = opendir(current);
    if (dir == NULL) {                                       // LCOV_EXCL_START
      LOG_PROGRESS { perror(current); }
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

      s_total_files_seen++;

      LOG_PROGRESS {
        if ((s_total_files_seen % 5000) == 0) {
          LOG(L_PROGRESS, "Files scanned: %" PRIu32 "\n", s_total_files_seen);
        }
      }

      // Skip files with 'path_separator' in them because dupd uses this
      // character as a separator in the sqlite duplicates table.

      if (strchr(entry->d_name, path_separator)) {
        LOG(L_PROGRESS, "SKIP (due to %c) [%s/%s]\n",
            path_separator, current, entry->d_name);
        s_files_skip_badsep++;
        continue;
      }

      if (curlen == 1 && current[0] == '/') {
        snprintf(newpath, DUPD_PATH_MAX, "/%s", entry->d_name);
      } else {
        snprintf(newpath, DUPD_PATH_MAX, "%s/%s", current, entry->d_name);
      }

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
      if (entry->d_type == DT_REG) {
        type = D_FILE;
      } else if (entry->d_type == DT_DIR) {
        type = D_DIR;
      }
#endif
      if (type == D_OTHER) {
        rv = get_file_info(newpath, &new_stat_info);
        size = (uint64_t)new_stat_info.st_size;
        inode = new_stat_info.st_ino;
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
        s_total_files_seen--;

        // If 'newpath' is a directory, save it in scan_list for later

        // But first, if one_file_system, check whether this dir is
        // on a different device. If so, ignore it.
        if (one_file_system) {
          get_file_info(newpath, &new_stat_info);
          if (device > 0 && new_stat_info.st_dev != device) {
            LOG(L_SKIPPED, "SKIP (--one-file-system) [%s]\n", newpath);
            break;
          }
        }

        scan_list_pos++;
        if (scan_list_pos > scan_list_usage_max) {
          scan_list_usage_max = scan_list_pos;
        }
        if (scan_list_pos == scan_list_capacity) {
          scan_list_resizes++;
          scan_list_capacity *= 2;
          scan_list = (struct scan_list_entry *)
            realloc(scan_list, sizeof(struct scan_list_entry) *
                    scan_list_capacity);
          LOG(L_RESOURCES, "Had to increase scan_list_capacity to %d\n",
              scan_list_capacity);
        }

        strlcpy(scan_list[scan_list_pos].path, newpath, DUPD_PATH_MAX);
        struct direntry * new_dir_entry =
          new_child_dir(entry->d_name, current_dir_entry);
        scan_list[scan_list_pos].dir_entry = new_dir_entry;

        LOG(L_TRACE, "queued dir at %d: %s\n", scan_list_pos, newpath);
        break;

      case D_FILE:
        // If it is a file, just process it now
        (*process_file)(dbh, inode, size, newpath,
                        entry->d_name, current_dir_entry);
        break;

      case D_OTHER:
        LOG(L_SKIPPED, "SKIP (not file) [%s]\n", newpath);
        stats_files_ignored++;
        s_files_skip_notfile++;
        break;

      case D_ERROR:
        LOG(L_PROGRESS, "SKIP (error) [%s]\n", newpath);
        stats_files_error++;
        s_files_skip_error++;
        break;
      }
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
  init_filecompare();
  init_sizetree();
  init_scanlist();
  init_dirtree();

  if (hdd_mode) { init_read_list(); }

  if (write_db) {
    dbh = open_database(db_path, 1);
    begin_transaction(dbh);
  }

  pthread_t status_thread;
  int started_status_thread = 0;
  if (log_level > L_NONE && log_level < L_PROGRESS) {
    if (pthread_create(&status_thread, NULL, scan_status, NULL)) {
                                                             // LCOV_EXCL_START
      printf("error: unable to create scan status thread!\n");
      exit(1);
    }                                                        // LCOV_EXCL_STOP
    started_status_thread = 1;
  }

  // If we're hoping to use fiemap, do a sanity check first.

  if (using_fiemap) {
    struct block_list * bl;
    void * fmap = fiemap_alloc();

    for (int i=0; using_fiemap && start_path[i] != NULL; i++) {
      int rv = get_file_info(start_path[i], &stat_info);
      if (rv == 0) {
        bl = get_block_info_from_path(start_path[i], stat_info.st_ino,
                                      stat_info.st_size, fmap);
        if (bl == NULL || bl->entry[0].block == 0) {
          using_fiemap = 0;
          LOG(L_PROGRESS, "Disabling use of fiemap (%s first block zero)\n",
              start_path[i]);
        }
        if (bl != NULL) { free(bl); }
      }
    }
    free(fmap);
    LOG(L_INFO, "Still using_fiemap, sanity check ok\n");
  }

  // Scan phase - stat all files and build size tree, size list and path list

  scan_phase_started = get_current_time_millis();
  for (int i=0; start_path[i] != NULL; i++) {
    int rv = get_file_info(start_path[i], &stat_info);
    if (rv != 0) {
      printf("error: skipping requested path [%s]\n", start_path[i]);
    } else {
      struct direntry * top = new_child_dir(start_path[i], NULL);
      if (threaded_sizetree) {
        walk_dir(dbh, start_path[i], top, stat_info.st_dev, add_queue);
      } else {
        walk_dir(dbh, start_path[i], top, stat_info.st_dev, add_file);
      }
    }
  }

  if (threaded_sizetree) {
    scan_done();
  }

  d_mutex_lock(&status_lock, "scan end");
  stats_time_scan = get_current_time_millis() - scan_phase_started;
  pthread_cond_signal(&status_cond);
  d_mutex_unlock(&status_lock);

  LOG(L_PROGRESS, "Files scanned: %" PRIu32 " (%ldms)\n",
      s_total_files_seen, stats_time_scan);

  if (s_total_files_seen == 0) {
    if (write_db) {
      commit_transaction(dbh);
      close_database(dbh);
    }
    stats_round_duration[ROUND1] = 0;
    stats_round_duration[ROUND2] = 0;
    return;
  }

  if (save_uniques) {
    LOG(L_PROGRESS, "Saving files with unique sizes from size tree...\n");
    find_unique_sizes(dbh);
  }

  long t1 = get_current_time_millis();
  sort_read_list(fiemap_ok);
  LOG_PROGRESS {
    long sort_time = get_current_time_millis() - t1;
    printf("Time to sort read list: %ldms\n", sort_time);
  }

  // Processing phase - walk through size list whittling down the potentials

  read_phase_started = get_current_time_millis();
  process_size_list(dbh);

  stats_time_process = get_current_time_millis() - read_phase_started;;

  if (stats_round_duration[ROUND1] < 0) { stats_round_duration[ROUND1] = 0; }
  if (stats_round_duration[ROUND2] < 0) { stats_round_duration[ROUND2] = 0; }

  LOG(L_PROGRESS, "Duplicate processing took %ldms\n", stats_time_process);
  LOG(L_PROGRESS, "Largest duplicate set %d\n", stats_most_dups);

  if (write_db) {
    commit_transaction(dbh);
    close_database(dbh);
  }

  LOG_RESOURCES {
    report_path_block_usage();
  }

  scan_completed = 1;
  if (started_status_thread) {
    d_join(status_thread, NULL);
  }

  report_stats();
}
