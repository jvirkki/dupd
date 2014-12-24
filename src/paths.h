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

#ifndef _DUPD_PATHS_H
#define _DUPD_PATHS_H


/** ***************************************************************************
 * Initialize path_block data structures.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void init_path_block();


/** ***************************************************************************
 * Free path_block data structures.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void free_path_block();


/** ***************************************************************************
 * Inserts the first file in a path list into the next available slot.
 * A path list consists of a linked list of all the files of the same
 * size.  File scan will call this when it encounters the first file
 * to have a given size. Subsequent files of the same size are added
 * with insert_end_path().
 *
 * Parameters:
 *    path - The path of the file to add. Gets copies into the path block.
 *
 * Return: Pointer to the path block entry now containing this path.
 *
 */
char * insert_first_path(char * path);


/** ***************************************************************************
 * Adds subsequent paths to a path list. The first path must have been added
 * by insert_first_path() earlier.
 *
 * If the path being added is the second path on this path list, the path list
 * gets added to the size list for processing.
 *
 * Parameters:
 *    path  - The path of the file to add. Gets copied into the path block.
 *    size  - The size of the files in this path list.
 *    first - The head of this path list (from insert_first_path() earlier).
 *
 * Return: none
 *
 */
void insert_end_path(char * path, long size, char * first);


/** ***************************************************************************
 * Print out some stats on path block usage.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void report_path_block_usage();


#endif
