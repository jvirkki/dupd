/*
  Copyright 2012-2018 Jyri J. Virkki <jyri@virkki.com>

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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


/** ***************************************************************************
 * Functions to manage the path lists.
 *
 * A path list is a linked list of the file paths of all files which have
 * the same size.
 *
 * The head of the path list (returned by insert_first_path()) points to
 * a path_list_head struct. Immediately following (in memory) there will
 * be the first path_list_entry.
 *
 */
struct path_list_entry {
  uint8_t state;
  uint8_t filename_size;
  struct direntry * dir;
  struct path_list_entry * next;
  char * buffer;
  // filename follows, file_size bytes
};

struct path_list_head {
  struct size_list * sizelist;
  struct path_list_entry * last_entry;
  uint16_t list_size;
  uint8_t state;
  // first_entry follows
};


// File State used in path_list_entry
#define FS_NEW 51
#define FS_R1_BUFFER_FILLED 52
#define FS_INVALID 53
#define FS_R1_DONE 54

// Path List State in path_list_head
#define PLS_NEW 11
#define PLS_R1_BUFFERS_FULL 12
#define PLS_R2_NEEDED 13
#define PLS_DONE 14


/** ***************************************************************************
 * Return string representation of path list state.
 *
 */
const char * pls_state(int state);


/** ***************************************************************************
 * Return string representation of file state.
 *
 */
const char * file_state(int state);


/** ***************************************************************************
 * Debug function. Dumps the path list for a given size starting from head.
 *
 */
void dump_path_list(const char * line, off_t size,
                    struct path_list_head * head);


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
 * Subsequent files of the same size are added with insert_end_path().
 *
 * Parameters:
 *    filename  - The filename of this first file.
 *    dir_entry - Directory containing filename.
 *
 * Return: Pointer to the head of this path list.
 *
 */
struct path_list_head * insert_first_path(char * filename,
                                          struct direntry * dir_entry);


/** ***************************************************************************
 * Adds subsequent paths to a path list. The first path must have been added
 * by insert_first_path() earlier.
 *
 * If the path being added is the second path on this path list, the path list
 * gets added to the size list for processing.
 *
 * Parameters:
 *    filename  - The filename of this file.
 *    dir_entry - Directory containing filename.
 *    block     - First physical block of file (zero if not used).
 *    inode     - The inode of this path.
 *    size      - The size of the files in this path list.
 *    head      - The head of this path list (from insert_first_path()).
 *
 * Return: none
 *
 */
void insert_end_path(char * filename, struct direntry * dir_entry,
                     uint64_t block, ino_t inode, off_t size,
                     struct path_list_head * head);


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
 * Given a path list head, return address of the first path block entry.
 *
 * Parameters:
 *    head - The path list head.
 *
 * Return: The first path list entry.
 *
 */
static inline struct path_list_entry *
pb_get_first_entry(struct path_list_head * head)
{
  return (struct path_list_entry *)
    ((char *)head + sizeof(struct path_list_head));
}


/** ***************************************************************************
 * Given a path list entry, return address of its filename buffer.
 *
 * Parameters:
 *     entry - The path list entry.
 *
 * Return: Address of the filename buffer.
 *
 */
static inline char * pb_get_filename(struct path_list_entry * entry)
{
  if (entry == NULL) { return NULL; }
  return (char *)((char *)entry + sizeof(struct path_list_entry));
}


/** ***************************************************************************
 * Return pointer to an entry containing a valid path given a starting point.
 * The node returned might be entry itself, if it contains a path. Or else,
 * the next available one.
 *
 * Parameters:
 *    entry - The entry
 *
 * Return: pointer to entry with valid path (or NULL if end of the path list)
 *
 */
static inline struct path_list_entry *
pl_entry_get_valid_node(struct path_list_entry * entry)
{
  struct path_list_entry * n = entry;
  char * p;

  do {
    p = pb_get_filename(n);
    if (p != NULL && p[0] != 0) {
      return n;
    }
    n = n->next;
  } while (n != NULL);

  return NULL;
}


#endif
