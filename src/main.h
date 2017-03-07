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

#include <pthread.h>
#include <sys/types.h>

#ifdef __APPLE__
#endif

#ifdef __linux__
#define FADVISE 1
#endif

#ifdef __sun__
#define FADVISE 1
#endif

#ifdef __OpenBSD__
#endif


/** ***************************************************************************
 * Verbosity is 1 by default, increased by one for every -v command
 * line argument. Higher values produce more diagnostic noise:
 *
 *  L_NONE = No output (-q option)
 *  L_BASE = (Default) Animated progress (brief)
 *  L_MORE = More animated progress data and summary stats
 *
 *  (Levels 3 and higher skip the animation to show lines of info.)
 *
 *  L_PROGRESS = Basic progress lines, non-fatal errors and show stats
 *  L_INFO = State, defaults, other low-volume info pre- and post-run
 *  L_MORE_INFO = Like info but more annoyingly verbose
 *  L_RESOURCES = Allocations, resize, other low-volume memory management
 *  L_THREADS = Producer/consumer thread activity
 *  L_SKIPPED = Files not read or processed
 *  L_MORE_THREADS = Noisier thread activity
 *  L_TRACE = Lots of output on everything that is going on
 *  L_FILES = Print every file read
 *  L_MORE_TRACE = Data structure dumps and such, too much noise
 *
 */
extern int log_level;

#define L_NONE 0
#define L_BASE 1
#define L_MORE 2
#define L_PROGRESS 3
#define L_INFO 4
#define L_MORE_INFO 5
#define L_RESOURCES 6
#define L_THREADS 7
#define L_SKIPPED 8
#define L_MORE_THREADS 9
#define L_TRACE 10
#define L_FILES 11
#define L_MORE_TRACE 12

#define LOG(level, ...)  if (level <= log_level) { \
    printf("%s", get_thread_name());               \
    printf(__VA_ARGS__);                           \
  }

#define LOG_BASE if (log_level >= L_BASE)
#define LOG_MORE if (log_level >= L_MORE)
#define LOG_PROGRESS if (log_level >= L_PROGRESS)
#define LOG_INFO if (log_level >= L_INFO)
#define LOG_MORE_INFO if (log_level >= L_MORE_INFO)
#define LOG_RESOURCES if (log_level >= L_RESOURCES)
#define LOG_THREADS if (log_level >= L_THREADS)
#define LOG_SKIPPED if (log_level >= L_SKIPPED)
#define LOG_MORE_THREADS if (log_level >= L_MORE_THREADS)
#define LOG_TRACE if (log_level >= L_TRACE)
#define LOG_MORE_TRACE if (log_level >= L_MORE_TRACE)


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
extern char _binary_man_dupd_start;
extern char _binary_man_dupd_end;
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
 * Report output format.
 *
 */
#define REPORT_FORMAT_TEXT 1
#define REPORT_FORMAT_CSV 2
#define REPORT_FORMAT_JSON 3
extern int report_format;


/** ***************************************************************************
 * Thread name used for logging (at L_THREADS and higher)
 *
 */
extern pthread_key_t thread_name;


/** ***************************************************************************
 * Used as the max path+filename length.
 *
 */
#define DUPD_PATH_MAX 4096


#endif
