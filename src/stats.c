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

int stats_sets_processed[ROUNDS] = { 0,0 };
int stats_sets_dup_done[ROUNDS] = { 0,0 };
int stats_sets_dup_not[ROUNDS] = { 0,0 };
int stats_sets_full_read[ROUNDS] = { 0,0 };
int stats_sets_part_read[ROUNDS] = { 0,0 };
long stats_round_start[ROUNDS] = { -1,-1 };
int stats_round_duration[ROUNDS] = { -1,-1 };
int stats_duplicate_groups[ROUNDS] = { 0,0 };
int stats_reader_loops[ROUNDS] = { 0,0 };
int stats_hasher_loops[ROUNDS][MAX_HASHER_THREADS] = { {0,0}, {0,0} };
int stats_hasher_queue_len[MAX_HASHER_THREADS] = { 0,0 };

uint64_t stats_total_bytes = 0;
uint64_t stats_total_bytes_read = 0;
uint64_t stats_total_bytes_hashed = 0;
uint64_t stats_comparison_bytes_read = 0;
uint32_t stats_max_pathlist = 0;
long stats_max_pathlist_size = 0;
uint32_t stats_path_list_entries = 0;
int stats_most_dups = 0;
int stats_duplicate_files = 0;

int stats_full_hash_first = 0;
int stats_full_hash_second = 0;
int stats_partial_hash_second = 0;
int stats_one_block_hash_first = 0;

int stats_size_list_count = 0;
int stats_size_list_done = 0;
int stats_three_file_compare = 0;
int stats_two_file_compare = 0;
int stats_uniques_saved = 0;
long stats_size_list_avg = 0;
uint32_t stats_files_count = 0;
int stats_files_ignored = 0;
int stats_files_error = 0;
long stats_avg_file_size = 0;
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


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void report_size_list()
{
  printf("Number of size sets to analyze: %d\n", stats_size_list_count);
  printf("Avg. size of files added to size list: %ld\n", stats_size_list_avg);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void report_stats()
{
  LOG_MORE {
    printf("\n");
    printf("Number of size sets to analyze: %d\n", stats_size_list_count);
    printf("Size sets with two files, hash list skipped: %d times\n",
           stats_two_file_compare);
    printf("Size sets with three files, hash list skipped: %d times\n",
           stats_three_file_compare);

    printf("\n");
    printf("Round one: hash list processed for %d size sets (%dms)\n",
           stats_sets_processed[ROUND1], stats_round_duration[ROUND1]);
    printf("  Block size %d (%d max blocks)\n",
           hash_one_block_size, hash_one_max_blocks);
    printf("  Sets fully hashed in round one: %d\n", stats_full_hash_first);
    printf("  Sets with single block first round: %d\n",
           stats_one_block_hash_first);
    printf("  Sets with dups ruled out in first round: %d\n",
           stats_sets_dup_not[ROUND1]);
    printf("  Sets with dups confirmed in first round: %d\n",
           stats_sets_dup_done[ROUND1]);
    printf("  Groups of dups confirmed in first round: %d\n",
           stats_duplicate_groups[ROUND1]);

    printf("Round two: hash list processed for %d size sets (%dms)\n",
           stats_sets_processed[ROUND2], stats_round_duration[ROUND2]);
    printf("  Reader loops: %d\n", stats_reader_loops[ROUND2]);
    printf("  Hasher loops:");
    for (int i = 0; i < MAX_HASHER_THREADS; i++) {
      printf("   %d", stats_hasher_loops[ROUND2][i]);
    }
    printf("\n");
    printf("  Sets with dups ruled out in second round: %d\n",
           stats_sets_dup_not[ROUND2]);
    printf("  Sets with dups confirmed in second round: %d\n",
           stats_sets_dup_done[ROUND2]);
    printf("  Groups of dups confirmed in second round: %d\n",
           stats_duplicate_groups[ROUND2]);

    printf("\n");
    printf("Total bytes of all files: %" PRIu64 "\n", stats_total_bytes);
    printf("Total bytes read from disk: %" PRIu64 " (%d%%)\n",
           stats_total_bytes_read,
           (int)((100 * stats_total_bytes_read) / stats_total_bytes));
    printf("  Total bytes hashed: %" PRIu64 " (%d%%)\n",
           stats_total_bytes_hashed,
           (int)((100 * stats_total_bytes_hashed) / stats_total_bytes));
    printf("  Total bytes directly compared: %" PRIu64 " (%d%%)\n",
           stats_comparison_bytes_read,
           (int)((100 * stats_comparison_bytes_read) / stats_total_bytes));

    printf("\n");
  }

  LOG_BASE {
    char timebuf[20];
    time_string(timebuf, 20, get_current_time_millis() - stats_main_start);
    int total_groups = stats_duplicate_groups[ROUND1] +
      stats_duplicate_groups[ROUND2];
    printf("Total duplicates: %d files in %d groups in %s\n",
           stats_duplicate_files, total_groups, timebuf);
    if (save_uniques) {
      printf("Total unique files: %d\n", stats_uniques_saved);
    }
    if (write_db && stats_duplicate_files > 0) {
      printf("Run 'dupd report' to list duplicates.\n");
    }
  }

  // Some sanity checking
  int totals_from_rounds =
    stats_sets_dup_not[ROUND1] + stats_sets_dup_done[ROUND1] +
    stats_sets_dup_not[ROUND2] + stats_sets_dup_done[ROUND2] +
    stats_two_file_compare + stats_three_file_compare;

  if (totals_from_rounds != stats_size_list_count) {         // LCOV_EXCL_START
    printf("\nerror: total size sets %d != sets confirmed %d\n",
           stats_size_list_count, totals_from_rounds);
    exit(1);
  }                                                          // LCOV_EXCL_STOP
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void save_stats()
{
  FILE * fp = fopen(stats_file, "a");
  fprintf(fp, "stats_total_bytes %" PRIu64 "\n", stats_total_bytes);
  fprintf(fp, "stats_total_bytes_read %" PRIu64 "\n", stats_total_bytes_read);
  fprintf(fp, "stats_total_bytes_hashed %" PRIu64 "\n",
          stats_total_bytes_hashed);
  fprintf(fp, "stats_comparison_bytes_read %" PRIu64 "\n",
          stats_comparison_bytes_read);
  fprintf(fp, "stats_duplicate_files %d\n", stats_duplicate_files);
  int total_groups = stats_duplicate_groups[ROUND1] +
    stats_duplicate_groups[ROUND2];
  fprintf(fp, "stats_duplicate_groups %d\n", total_groups);
  fprintf(fp, "stats_duplicate_groups_1 %d\n", stats_duplicate_groups[ROUND1]);
  fprintf(fp, "stats_duplicate_groups_2 %d\n", stats_duplicate_groups[ROUND2]);
  fprintf(fp, "stats_full_hash_first %d\n", stats_full_hash_first);
  fprintf(fp, "stats_full_hash_second %d\n", stats_full_hash_second);
  fprintf(fp, "stats_partial_hash_second %d\n", stats_partial_hash_second);
  fprintf(fp, "stats_one_block_hash_first %d\n", stats_one_block_hash_first);
  fprintf(fp, "stats_set_dups_done_round_one %d\n",
          stats_sets_dup_done[ROUND1]);
  fprintf(fp, "stats_set_dups_done_round_two %d\n",
          stats_sets_dup_done[ROUND2]);
  fprintf(fp, "stats_set_no_dups_round_one %d\n", stats_sets_dup_not[ROUND1]);
  fprintf(fp, "stats_set_no_dups_round_two %d\n", stats_sets_dup_not[ROUND2]);
  fprintf(fp, "stats_set_round_one %d\n", stats_sets_processed[ROUND1]);
  fprintf(fp, "stats_set_round_two %d\n", stats_sets_processed[ROUND2]);
  fprintf(fp, "stats_size_list_count %d\n", stats_size_list_count);
  fprintf(fp, "stats_three_file_compare %d\n", stats_three_file_compare);
  fprintf(fp, "stats_two_file_compare %d\n", stats_two_file_compare);
  fprintf(fp, "stats_uniques_saved %d\n", stats_uniques_saved);
  fprintf(fp, "stats_size_list_avg %ld\n", stats_size_list_avg);
  fprintf(fp, "stats_files_count %" PRIu32 "\n", stats_files_count);
  fprintf(fp, "stats_files_ignored %d\n", stats_files_ignored);
  fprintf(fp, "stats_files_error %d\n", stats_files_error);
  fprintf(fp, "stats_avg_file_size %ld\n", stats_avg_file_size);
  fprintf(fp, "stats_time_scan %ld\n", stats_time_scan);
  fprintf(fp, "stats_time_process %ld\n", stats_time_process);
  fprintf(fp, "stats_time_total %ld\n", stats_time_total);
  fprintf(fp, "hash_one_block_size %d\n", hash_one_block_size);
  fprintf(fp, "hash_one_max_blocks %d\n", hash_one_max_blocks);
  fprintf(fp, "hash_block_size %d\n", hash_block_size);
  fprintf(fp, "path_buffer_realloc %d\n", path_buffer_realloc);
  fprintf(fp, "stats_hashlist_path_realloc %d\n", stats_hashlist_path_realloc);
  fprintf(fp, "stats_path_list_entries %" PRIu32 "\n",stats_path_list_entries);
  fprintf(fp, "stats_hash_list_len_inc %d\n", stats_hash_list_len_inc);
  fprintf(fp, "scan_list_usage_max %d\n", scan_list_usage_max);
  fprintf(fp, "scan_list_resizes %d\n", scan_list_resizes);
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
