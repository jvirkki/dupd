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

#ifndef _DUPD_MAIN_H
#define _DUPD_MAIN_H

#include <sys/types.h>


/** ***************************************************************************
 * Verbosity is 1 by default, increased by one for every -v command
 * line argument. Higher values produce more diagnostic noise:
 *
 *  0 = No output (-q option)
 *  1 = (Default) Brief end-user status lines only.
 *  2 = Adds some timing info and more status, still brief output.
 *  3 = Adds file activity for new/changed files
 *  4 = Adds all scan activity
 *  5 = And the kitchen sink
 *
 */
extern int verbosity;


/** ***************************************************************************
 * Verbosity for thread state messages, increased by one for every -V command
 * line argument. Higher values produce more diagnostic noise:
 *
 *  0 = No output (default)
 *  1 = Thread state info.
 *  2 = Additional state variables.
 *
 */
extern int thread_verbosity;


/** ***************************************************************************
 * Scanning starts here.
 *
 */
extern char * start_path[];


/** ***************************************************************************
 * A file specified by the user.
 *
 */
extern char * file_path;


/** ***************************************************************************
 * Duplicate info will be saved in the sqlite database unless this is false.
 *
 */
extern int write_db;


/** ***************************************************************************
 * Path to the sqlite database.
 *
 */
extern char * db_path;


/** ***************************************************************************
 * This cut_path is used by report operation to optionally remove matching
 * path components to reduce output size.
 *
 */
extern char * cut_path;


/** ***************************************************************************
 * When reporting duplicates, if exclude_path is defined, any duplicates
 * contained within this tree are ignored (not considered duplicates).
 *
 */
extern char * exclude_path;
extern int exclude_path_len;


/** ***************************************************************************
 * This minimum_file_size is the smallest size handled by scan or report.
 *
 */
extern off_t minimum_file_size;


/** ***************************************************************************
 * If true, in sets with only two files the files are compared directly
 * skipping the hash list processing entirely.
 *
 */
extern int opt_compare_two;


/** ***************************************************************************
 * If true, in sets with only three files the files are compared directly
 * skipping the hash list processing entirely.
 *
 */
extern int opt_compare_three;


/** ***************************************************************************
 * Maximum number of blocks read by the first pass.
 *
 */
extern int hash_one_max_blocks;


/** ***************************************************************************
 * Size of blocks to read from disk during first pass.
 *
 */
extern int hash_one_block_size;


/** ***************************************************************************
 * The second hashing pass is done on this many blocks.
 * If this value is less than hash_one_max_blocks, the intermediate list
 * is skipped.
 *
 */
extern int intermediate_blocks;


/** ***************************************************************************
 * Size of blocks to read from disk during subsequent passes.
 *
 */
extern int hash_block_size;


/** ***************************************************************************
 * Size of blocks to read from disk during direct file comparisons.
 *
 */
extern int filecmp_block_size;


/** ***************************************************************************
 * Expected total number of files to scan.
 *
 */
extern long file_count;


/** ***************************************************************************
 * HDD mode if true.
 *
 */
extern int hdd_mode;


/** ***************************************************************************
 * Start and end of the usage info document buffer.
 *
 */
#ifndef __APPLE__
extern char _binary_USAGE_start;
extern char _binary_USAGE_end;
#endif


/** ***************************************************************************
 * If true, save files found to be unique during a scan in the database.
 *
 */
extern int save_uniques;


/** ***************************************************************************
 * If true, current database has info on unique files.
 *
 */
extern int have_uniques;


/** ***************************************************************************
 * If true, ignore unique table info even if have_uniques is true.
 *
 */
extern int no_unique;


/** ***************************************************************************
 * Save stats to this file if defined.
 *
 */
extern char * stats_file;


/** ***************************************************************************
 * Character used as pathname separator when saving multiple paths to db.
 * path_sep_string contains same in null-terminated string form.
 *
 */
extern int path_separator;
extern char * path_sep_string;


/** ***************************************************************************
 * If true, include hidden files and directories in the scan.
 *
 */
extern int scan_hidden;


/** ***************************************************************************
 * If true, use smaller defaults for memory buffers. This is useful only
 * for testing in order to force reallocations earlier.
 *
 */
extern int x_small_buffers;


/** ***************************************************************************
 * If true, run the analyze_process_size_list() after scan. This is for
 * testing only and disables other functionality (such as threading).
 *
 */
extern int x_analyze;


/** ***************************************************************************
 * If true, enable behavior(s) that only make sense while testing.
 *
 */
extern int only_testing;


/** ***************************************************************************
 * If true, add to sizetree (while scanning) in a separate thread.
 *
 */
extern int threaded_sizetree;


/** ***************************************************************************
 * If true, read from disk (while hashing and comparing) in a separate thread.
 *
 */
extern int threaded_hashcompare;


/** ***************************************************************************
 * If database is older than this, show a warning.
 *
 */
extern long db_warn_age_seconds;


/** ***************************************************************************
 * If true, generate links in rmsh operation.
 *
 */
#define RMSH_LINK_SOFT 1
#define RMSH_LINK_HARD 2
extern int rmsh_link;


/** ***************************************************************************
 * If true, hard links are considered unique files.
 *
 */
extern int hardlink_is_unique;


/** ***************************************************************************
 * Hash function to use.
 *
 */
extern int hash_function;


/** ***************************************************************************
 * Output size of hash_function.
 *
 */
extern int hash_bufsize;


/** ***************************************************************************
 * Used as the max path+filename length.
 *
 */
#define PATH_MAX 4096


#endif
