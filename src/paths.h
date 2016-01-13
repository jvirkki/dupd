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

#ifndef _DUPD_PATHS_H
#define _DUPD_PATHS_H

#include <inttypes.h>

/** ***************************************************************************
 * Functions to manage the path lists.
 *
 * A path list is a linked list of the file paths of all files which have
 * the same size.
 *
 * The head of the path list (returned by insert_first_path()) points to:
 *
 *      head
 *       |
 *    +--v-----+--------+--------+-------------------+
 *    |PTR2LAST|ListSize|PTR2NEXT|path string[0] ... |
 *    +--------+--------+----+---+-------------------+
 *                           |
 *                      +----v---+-------------------+
 *                      |PTR2NEXT|path string[1] ... |
 *                      +--------+-------------------+
 *
 */


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
 * File scan will call this when it encounters the first file to have
 * a given size. Subsequent files of the same size are added with
 * insert_end_path().
 *
 * Parameters:
 *    path - The path of the file to add. Gets copies into the path block.
 *
 * Return: Pointer to the head of this path list.
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
 *    head  - The head of this path list (from insert_first_path() earlier).
 *
 * Return: none
 *
 */
void insert_end_path(char * path, long size, char * head);


/** ***************************************************************************
 * Print out some stats on path block usage.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void report_path_block_usage();


/** ***************************************************************************
 * Return pointer to the first entry of a pathlist.
 *
 * Parameters:
 *    head - head of this pathlist
 *
 * Return: pointer to first entry
 *
 */
static inline char * pl_get_first_entry(char * head)
{
  return head + 2 * sizeof(char *);
}


/** ***************************************************************************
 * Return pointer to the last entry of a pathlist.
 *
 * Parameters:
 *    head - head of this pathlist
 *
 * Return: pointer to last entry
 *
 */
static inline char * pl_get_last_entry(char * head)
{
  return *(char **)head;
}


/** ***************************************************************************
 * Initialize path count to one in a new pathlist.
 *
 * Parameters:
 *    head - head of this pathlist
 *
 * Return: none
 *
 */
static inline void pl_init_path_count(char * head)
{
  char * list_len = head + sizeof(char *);
  *(uint32_t *)list_len = 1;
}


/** ***************************************************************************
 * Return the number of entries in this pathlist.
 *
 * Parameters:
 *    head - head of this pathlist
 *
 * Return: number of entries
 *
 */
static inline uint32_t pl_get_path_count(char * head)
{
  char * list_len = head + sizeof(char *);
  return(uint32_t)*(uint32_t *)list_len;
}


/** ***************************************************************************
 * Increase the number of entries in this pathlist by one.
 *
 * Parameters:
 *    head - head of this pathlist
 *
 * Return: updated number of entries
 *
 */
static inline uint32_t pl_increase_path_count(char * head)
{
  char * list_len = head + 1 * sizeof(char *);
  uint32_t path_count = (uint32_t)*(uint32_t *)list_len;
  path_count++;
  *(uint32_t *)list_len = path_count;
  return path_count;
}


/** ***************************************************************************
 * Given an entry, set its next pointer.
 *
 * Parameters:
 *    entry - The entry to update.
 *    next  - The next entry to point at.
 *
 * Return: none
 *
 */
static inline void pl_entry_set_next(char * entry, char * next)
{
  *(char **)entry = next;
}


/** ***************************************************************************
 * Return pointer to the path string of an entry.
 *
 * Parameters:
 *    entry - The entry
 *
 * Return: pointer to path string location
 *
 */
static inline char * pl_entry_get_path(char * entry)
{
  return entry + sizeof(char *);
}


/** ***************************************************************************
 * Return pointer to the next entry given an entry.
 *
 * Parameters:
 *    entry - The entry
 *
 * Return: pointer to next entry (or NULL)
 *
 */
static inline char * pl_entry_get_next(char * entry)
{
  return *(char **)entry;
}


#endif
