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

int stats_all_blocks_count = 0;
int stats_duplicate_files = 0;
int stats_duplicate_sets = 0;
int stats_full_hash_first = 0;
int stats_mid_blocks_count = 0;
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
int stats_single_block_count = 0;
int stats_size_list_count = 0;
int stats_three_file_compare = 0;
int stats_two_file_compare = 0;
int stats_uniques_saved = 0;
long stats_comparison_blocks_read = 0;
long stats_hash_blocks_read = 0;
long stats_size_list_avg = 0;


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
    printf("Size sets with two files, hash list skipped: %d times\n",
           stats_two_file_compare);
    printf("Size sets with three files, hash list skipped: %d times\n",
           stats_three_file_compare);

    printf("Round one: hash list processed for %d size sets\n",
           stats_set_round_one);
    printf("  Sets fully hashed in round one: %d\n", stats_full_hash_first);
    printf("  Sets with single block first round: %d\n",
           stats_one_block_hash_first);
    printf("  Sets with dups ruled out in first round: %d\n",
           stats_set_no_dups_round_one);
    printf("  Sets with dups confirmed in first round: %d\n",
           stats_set_dups_done_round_one);

    printf("Round two: hash list processed for %d size sets\n",
           stats_set_round_two);
    printf("  Sets with dups ruled out in second round: %d\n",
           stats_set_no_dups_round_two);
    printf("  Sets with dups confirmed in second round: %d\n",
           stats_set_dups_done_round_two);

    printf("Round three: hash list processed for %d size sets\n",
           stats_set_full_round);
    printf("  Sets with dups ruled out in full round: %d\n",
           stats_set_no_dups_full_round);
    printf("  Sets with dups confirmed in full round: %d\n",
           stats_set_dups_done_full_round);

    printf("Read %ld blocks from disk for direct comparisons.\n",
           stats_comparison_blocks_read);

    printf("Computed single hash block for %d files.\n",
           stats_single_block_count);
    printf("Computed partial hash for %d files.\n", stats_mid_blocks_count);
    printf("Computed full hash for %d files.\n", stats_all_blocks_count);
    printf("Read and hashed a total of %ld blocks from disk.\n",
           stats_hash_blocks_read);

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
