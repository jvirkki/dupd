/*
  Copyright 2012 Jyri J. Virkki <jyri@virkki.com>

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


/** ***************************************************************************
 * Verbosity is 1 by default, increased by one for every -v command
 *  line argument. Higher values produce more diagnostic noise:
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
 * Scanning starts here.
 *
 */
extern char * start_path[];


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
 * This minimum_report_size is the smallest total size consumed by duplicates
 * to be shown by report.
 *
 */
extern int minimum_report_size;


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
 * The second hashing pass is done on this many blocks.
 * If this value is less than hash_one_max_blocks, the intermediate list
 * is skipped.
 *
 */
extern int intermediate_blocks;


/** ***************************************************************************
 * The path buffer is allocated a given size so it needs to be large enough.
 * It will be sized to hold file_count files with an average path length
 * of avg_path_len.
 *
 */
extern long file_count;
extern int avg_path_len;


/** ***************************************************************************
 * Start and end of the usage info document buffer.
 *
 */
#ifndef __APPLE__
extern char _binary_USAGE_start;
extern char _binary_USAGE_end;
#endif

/** ***************************************************************************
 * Used as the max path+filename length.
 *
 */
#define PATH_MAX 4096


#endif
