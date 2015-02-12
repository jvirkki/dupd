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

#ifndef _DUPD_STATS_H
#define _DUPD_STATS_H

#include <stdint.h>
#include <inttypes.h>

extern uint64_t stats_total_bytes;
extern uint64_t stats_total_bytes_read;
extern uint64_t stats_total_bytes_hashed;
extern uint64_t stats_comparison_bytes_read;
extern uint32_t stats_max_pathlist;
extern int stats_most_dups;
extern int stats_all_blocks_count;
extern int stats_duplicate_files;
extern int stats_duplicate_sets;
extern int stats_full_hash_first;
extern int stats_mid_blocks_count;
extern int stats_one_block_hash_first;
extern int stats_set_dups_done_full_round;
extern int stats_set_dups_done_round_one;
extern int stats_set_dups_done_round_two;
extern int stats_set_full_round;
extern int stats_set_no_dups_full_round;
extern int stats_set_no_dups_round_one;
extern int stats_set_no_dups_round_two;
extern int stats_set_round_one;
extern int stats_set_round_two;
extern int stats_single_block_count;
extern int stats_size_list_count;
extern int stats_three_file_compare;
extern int stats_two_file_compare;
extern int stats_uniques_saved;
extern long stats_size_list_avg;
extern long stats_files_count;
extern int stats_files_ignored;
extern int stats_files_error;
extern long stats_avg_file_size;
extern long stats_time_scan;
extern long stats_time_process;
extern long stats_time_total;


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
