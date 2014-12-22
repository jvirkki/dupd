/*
  Copyright 2012-2014 Jyri J. Virkki <jyri@virkki.com>

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
extern long stats_comparison_blocks_read;
extern long stats_hash_blocks_read;
extern long stats_size_list_avg;
extern unsigned long long stats_blocks_all_files;


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
 */void report_stats();


#endif
