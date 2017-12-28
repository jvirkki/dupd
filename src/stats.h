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

#ifndef _DUPD_STATS_H
#define _DUPD_STATS_H

#include <stdint.h>
#include <inttypes.h>

#define ROUNDS 3
#define ROUND1 0
#define ROUND2 1
#define ROUND3 2
#define MAX_HASHER_THREADS 2

extern int stats_sets_processed[ROUNDS];
extern int stats_sets_dup_done[ROUNDS];
extern int stats_sets_dup_not[ROUNDS];
extern int stats_sets_full_read[ROUNDS];
extern int stats_sets_part_read[ROUNDS];
extern long stats_round_start[ROUNDS];
extern int stats_round_duration[ROUNDS];
extern int stats_duplicate_groups[ROUNDS];
extern int stats_reader_loops[ROUNDS];
extern int stats_hasher_loops[ROUNDS][MAX_HASHER_THREADS];

extern uint64_t stats_total_bytes;
extern uint64_t stats_total_bytes_read;
extern uint64_t stats_total_bytes_hashed;
extern uint64_t stats_comparison_bytes_read;
extern uint32_t stats_max_pathlist;
extern long stats_max_pathlist_size;
extern uint32_t stats_path_list_entries;
extern int stats_most_dups;
extern int stats_all_blocks_count;
extern int stats_duplicate_files;
extern int stats_full_hash_first;
extern int stats_full_hash_second;
extern int stats_mid_blocks_count;
extern int stats_one_block_hash_first;

extern int stats_partial_hash_second;
extern int stats_set_dups_done_full_round;
extern int stats_set_dup_done_round_one;
extern int stats_set_dup_done_round_two;
extern int stats_set_full_round;
extern int stats_set_no_dups_full_round;
extern int stats_set_no_dups_round_one;
extern int stats_set_no_dups_round_two;
extern int stats_set_round_one;
extern int stats_set_round_two;
extern int stats_single_block_count;
extern int stats_size_list_count;
extern int stats_size_list_done;
extern int stats_three_file_compare;
extern int stats_two_file_compare;
extern int stats_uniques_saved;
extern long stats_size_list_avg;
extern uint32_t stats_files_count;
extern int stats_files_ignored;
extern int stats_files_error;
extern long stats_avg_file_size;
extern long stats_time_scan;
extern long stats_time_process;
extern long stats_time_total;
extern long stats_main_start;
extern int path_buffer_realloc;
extern int hashlist_path_realloc;
extern int hash_list_len_inc;
extern int scan_list_usage_max;
extern int scan_list_resizes;
extern int stats_analyzer_one_block;
extern int stats_analyzer_all_blocks;
extern int stats_analyzer_buckets[20];


/** ***************************************************************************
 * Print out some stats on size list.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void report_size_list();


/** ***************************************************************************
 * Print some stats to stdout depending on verbosity.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void report_stats();


/** ***************************************************************************
 * Save stats to a disk file.
 *
 */
void save_stats();


#endif
