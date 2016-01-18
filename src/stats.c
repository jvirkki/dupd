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

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "main.h"
#include "stats.h"

uint64_t stats_total_bytes = 0;
uint64_t stats_total_bytes_read = 0;
uint64_t stats_total_bytes_hashed = 0;
uint64_t stats_comparison_bytes_read = 0;
uint32_t stats_max_pathlist = 0;
long stats_max_pathlist_size = 0;
int stats_most_dups = 0;
int stats_duplicate_files = 0;
int stats_duplicate_sets = 0;
int stats_full_hash_first = 0;
int stats_one_block_hash_first = 0;
int stats_set_dups_done_full_round = 0;
int stats_set_dups_done_round_one = 0;
int stats_set_dups_done_round_two = 0;
int stats_set_full_round = 0;
int stats_set_no_dups_full_round = 0;
int stats_set_no_dups_round_one = 0;
int stats_set_no_dups_round_two = 0;
int stats_set_round_one = 0;
int stats_set_round_two = 0;
int stats_size_list_count = 0;
int stats_three_file_compare = 0;
int stats_two_file_compare = 0;
int stats_uniques_saved = 0;
long stats_size_list_avg = 0;
uint32_t stats_files_count = 0;
int stats_files_ignored = 0;
int stats_files_error = 0;
long stats_avg_file_size = 0;
long stats_time_scan = 0;
long stats_time_process = 0;
long stats_time_total = 0;
int path_buffer_realloc = 0;
int hashlist_path_realloc = 0;
int hash_list_len_inc = 0;
int scan_list_usage_max = 0;
int scan_list_resizes = 0;


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
  if (verbosity >= 2) {
    printf("\n");
    printf("Size sets with two files, hash list skipped: %d times\n",
           stats_two_file_compare);
    printf("Size sets with three files, hash list skipped: %d times\n",
           stats_three_file_compare);

    printf("\n");
    printf("Round one: hash list processed for %d size sets\n",
           stats_set_round_one);
    printf("  Block size %d (%d max blocks)\n",
           hash_one_block_size, hash_one_max_blocks);
    printf("  Sets fully hashed in round one: %d\n", stats_full_hash_first);
    printf("  Sets with single block first round: %d\n",
           stats_one_block_hash_first);
    printf("  Sets with dups ruled out in first round: %d\n",
           stats_set_no_dups_round_one);
    printf("  Sets with dups confirmed in first round: %d\n",
           stats_set_dups_done_round_one);

    printf("Round two: hash list processed for %d size sets\n",
           stats_set_round_two);
    printf("  Block size %d (%d max blocks)\n",
           hash_block_size, intermediate_blocks);
    printf("  Sets with dups ruled out in second round: %d\n",
           stats_set_no_dups_round_two);
    printf("  Sets with dups confirmed in second round: %d\n",
           stats_set_dups_done_round_two);

    printf("Round three: hash list processed for %d size sets\n",
           stats_set_full_round);
    printf("  Block size %d\n", hash_block_size);
    printf("  Sets with dups ruled out in full round: %d\n",
           stats_set_no_dups_full_round);
    printf("  Sets with dups confirmed in full round: %d\n",
           stats_set_dups_done_full_round);

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
    printf("Number of sets with duplicates: %d\n", stats_duplicate_sets);
  }

  if (verbosity >= 1) {
    printf("Total duplicates: %d\n", stats_duplicate_files);
    if (save_uniques) {
      printf("Total unique files: %d\n", stats_uniques_saved);
    }
    if (write_db && stats_duplicate_files > 0) {
      printf("Run 'dupd report' to list duplicates.\n");
    }
  }

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
  fprintf(fp, "stats_duplicate_sets %d\n", stats_duplicate_sets);
  fprintf(fp, "stats_full_hash_first %d\n", stats_full_hash_first);
  fprintf(fp, "stats_one_block_hash_first %d\n", stats_one_block_hash_first);
  fprintf(fp, "stats_set_dups_done_full_round %d\n",
          stats_set_dups_done_full_round);
  fprintf(fp, "stats_set_dups_done_round_one %d\n",
          stats_set_dups_done_round_one);
  fprintf(fp, "stats_set_dups_done_round_two %d\n",
          stats_set_dups_done_round_two);
  fprintf(fp, "stats_set_full_round %d\n", stats_set_full_round);
  fprintf(fp, "stats_set_no_dups_full_round %d\n",
          stats_set_no_dups_full_round);
  fprintf(fp, "stats_set_no_dups_round_one %d\n", stats_set_no_dups_round_one);
  fprintf(fp, "stats_set_no_dups_round_two %d\n", stats_set_no_dups_round_two);
  fprintf(fp, "stats_set_round_one %d\n", stats_set_round_one);
  fprintf(fp, "stats_set_round_two %d\n", stats_set_round_two);
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
  fprintf(fp, "intermediate_blocks %d\n", intermediate_blocks);
  fprintf(fp, "path_buffer_realloc %d\n", path_buffer_realloc);
  fprintf(fp, "hashlist_path_realloc: %d\n", hashlist_path_realloc);
  fprintf(fp, "hash_list_len_inc %d\n", hash_list_len_inc);
  fprintf(fp, "scan_list_usage_max %d\n", scan_list_usage_max);
  fprintf(fp, "scan_list_resizes %d\n", scan_list_resizes);
  fprintf(fp, "\n");
  fclose(fp);
}
