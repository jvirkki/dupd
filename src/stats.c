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

#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "main.h"
#include "stats.h"
#include "utils.h"

pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t counters_lock = PTHREAD_MUTEX_INITIALIZER;

int stats_sets_processed[ROUNDS] = { 0,0 };
int stats_sets_dup_done[ROUNDS] = { 0,0 };
int stats_sets_dup_not[ROUNDS] = { 0,0 };
int stats_sets_full_read[ROUNDS] = { 0,0 };
int stats_sets_part_read[ROUNDS] = { 0,0 };
long stats_round_start[ROUNDS] = { -1,-1 };
int stats_round_duration[ROUNDS] = { -1,-1 };
int stats_duplicate_groups = 0;
int stats_reader_loops[ROUNDS] = { 0,0 };
int stats_hasher_loops[ROUNDS][MAX_HASHER_THREADS] = { {0,0}, {0,0} };
int stats_hasher_queue_len[MAX_HASHER_THREADS] = { 0,0 };

long stats_process_start = -1;
long stats_process_duration = -1;

uint64_t stats_total_bytes = 0;
uint64_t stats_total_bytes_read = 0;
uint64_t stats_total_bytes_hashed = 0;
uint64_t stats_comparison_bytes_read = 0;
uint32_t stats_max_pathlist = 0;
uint64_t stats_max_pathlist_size = 0;
uint32_t stats_path_list_entries = 0;
int stats_most_dups = 0;
int stats_duplicate_files = 0;

int stats_full_hash_first = 0;
int stats_full_hash_second = 0;
int stats_partial_hash_second = 0;
int stats_one_block_hash_first = 0;


int stats_size_list_done = 0;
int stats_three_file_compare = 0;
int stats_two_file_compare = 0;
int stats_uniques_saved = 0;
long stats_size_list_avg = 0;

int stats_files_ignored = 0;
int stats_files_error = 0;
long stats_time_scan = -1;
long stats_time_process = 0;
long stats_time_total = 0;
long stats_main_start = 0;
int path_buffer_realloc = 0;
int stats_hashlist_path_realloc = 0;
int stats_hash_list_len_inc = 0;
int scan_list_usage_max = 0;
int scan_list_resizes = 0;
uint64_t stats_read_buffers_allocated = 0;
int stats_flusher_active = 0;
uint32_t stats_fiemap_total_blocks = 0;
uint32_t stats_fiemap_zero_blocks = 0;

uint32_t count_sets_first_read = 0;
uint32_t count_files_completed = 0;
uint32_t stats_sets_first_read_completed = 0;



// Keep from here after revamp
uint32_t s_stats_size_list_count = 0;   // Total size sets processed

uint32_t s_total_files_seen = 0;        // All file entries seen during scan
uint32_t s_files_skip_error = 0;        // Files skipped due to error
uint32_t s_files_skip_notfile = 0;      // Files skipped, not a file
uint32_t s_files_skip_badsep = 0;       // Files skipped, separator conflict
uint32_t s_files_cant_read = 0;         // Files skipped, can't read
uint32_t s_files_hl_skip = 0;           // Files skipped, hardlink-is-unique
uint32_t s_files_too_small = 0;         // Files skipped, too small
uint32_t s_files_in_sizetree = 0;       // Files added to size tree
uint32_t s_files_processed = 0;         // Files entered to path list
uint32_t s_files_completed_dups = 0;    // Files processed, found to be dups
uint32_t s_files_completed_unique = 0;  // Files processed, found to be unique

uint32_t stats_size_list_done_from_cache = 0; // Size sets done from cache

int current_open_files = 0;


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void report_stats()
{
  LOG_BASE {
    printf("\n");
    char timebuf[20];
    time_string(timebuf, 20, get_current_time_millis() - stats_main_start);
    printf("Total duplicates: %d files in %d groups in %s\n",
           s_files_completed_dups, stats_duplicate_groups, timebuf);
    if (stats_duplicate_files > 0) {
      printf("Run 'dupd report' to list duplicates.\n");
    }
  }

  uint32_t files_accepted = s_total_files_seen - s_files_too_small -
    s_files_skip_notfile - s_files_skip_error - s_files_skip_badsep -
    s_files_hl_skip;
  uint32_t unique_files = s_files_in_sizetree - s_files_processed;

  LOG_MORE {
    printf("\n");
    printf("Total files seen: %" PRIu32 "\n", s_total_files_seen);
    printf(" (too small: %" PRIu32 ", not file: %"
           PRIu32 ", errors: %" PRIu32 ", skip: %" PRIu32 ", hl_skip: %"
           PRIu32 ")\n",
           s_files_too_small, s_files_skip_notfile,
           s_files_skip_error, s_files_skip_badsep, s_files_hl_skip);

    printf("Files queued for processing: %" PRIu32 " in %" PRIu32 " sets\n",
           files_accepted, s_stats_size_list_count);


    printf(" (files with unique size: %" PRIu32 ")\n", unique_files);
    printf("Total files to process: %" PRIu32 "\n", s_files_processed);
    printf(" Duplicate files: %" PRIu32 "\n", s_files_completed_dups);
    printf(" Unique files: %" PRIu32 "\n", s_files_completed_unique);
    printf(" Unable to read: %" PRIu32 "\n", s_files_cant_read);
    if (hardlink_is_unique) {
      printf(" Skipped hardlinks: %" PRIu32 "\n", s_files_hl_skip);
    }
  }

  if (files_accepted != s_files_in_sizetree - s_files_hl_skip) {
    printf("error: mismatch files_accepted: %" PRIu32
           " != files in sizetree: %" PRIu32 "\n",
           files_accepted, s_files_in_sizetree - s_files_hl_skip);
    exit(1);
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void save_stats()
{
  FILE * fp = fopen(stats_file, "a");
  // TODO needs cleaning up
  fprintf(fp, "using_fiemap %d\n", using_fiemap);
  fprintf(fp, "fiemap_total_blocks %" PRIu32 "\n", stats_fiemap_total_blocks);
  fprintf(fp, "fiemap_zero_blocks %" PRIu32 "\n", stats_fiemap_zero_blocks);
  fprintf(fp, "duplicate_files %" PRIu32 "\n", s_files_completed_dups);
  fprintf(fp, "duplicate_groups %" PRIu32 "\n", stats_duplicate_groups);


  fprintf(fp, "size_list_done_from_cache %" PRIu32 "\n",
          stats_size_list_done_from_cache);

  fprintf(fp, "\n");
  fclose(fp);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void inc_stats_read_buffers_allocated(int bytes)
{
  d_mutex_lock(&stats_lock, "increasing buffers");
  stats_read_buffers_allocated += bytes;
  d_mutex_unlock(&stats_lock);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void dec_stats_read_buffers_allocated(int bytes)
{
  d_mutex_lock(&stats_lock, "decreasing buffers");
  stats_read_buffers_allocated -= bytes;
  d_mutex_unlock(&stats_lock);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void increase_unique_counter(int n)
{
  d_mutex_lock(&counters_lock, "counters");
  s_files_completed_unique += n;
  d_mutex_unlock(&counters_lock);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void increase_dup_counter(int n)
{
  d_mutex_lock(&counters_lock, "counters");
  s_files_completed_dups += n;
  d_mutex_unlock(&counters_lock);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void increase_sets_first_read()
{
  d_mutex_lock(&counters_lock, "counters");
  count_sets_first_read++;
  d_mutex_unlock(&counters_lock);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void increase_sets_first_read_completed()
{
  d_mutex_lock(&counters_lock, "counters");
  stats_sets_first_read_completed++;
  d_mutex_unlock(&counters_lock);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void update_open_files(int n)
{
  d_mutex_lock(&counters_lock, "counters");
  current_open_files += n;
  d_mutex_unlock(&counters_lock);
}
