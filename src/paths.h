/*
  Copyright 2012-2020 Jyri J. Virkki <jyri@virkki.com>

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
  struct path_list_entry * next;
  struct block_list * blocks;
  struct direntry * dir;
  char * buffer;
  void * hash_ctx;
  uint64_t file_pos;
  uint64_t next_read_byte;
  uint32_t next_buffer_pos;
  uint32_t bufsize;
  uint32_t data_in_buffer;
  int fd;
  uint8_t state;
  uint8_t filename_size;
  uint8_t next_read_block;
  // filename follows, file_size bytes
};

struct path_list_head {
  struct size_list * sizelist;
  struct path_list_entry * last_entry;
  uint32_t wanted_bufsize;
  uint16_t list_size;
  uint16_t buffer_ready;
  uint8_t state;
  uint8_t hash_passes;
  uint8_t have_cached_hashes;
  // first_entry follows
};


// File State used in path_list_entry
#define FS_NEED_DATA 51
#define FS_BUFFER_READY 53
#define FS_DONE 57
#define FS_CACHE_DONE 58
#define FS_UNIQUE 59
#define FS_IGNORE 60
#define FS_IGNORE_HL 62

// Path List State in path_list_head
#define PLS_NEED_DATA 14
#define PLS_ALL_BUFFERS_READY 18
#define PLS_DONE 21


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
void dump_path_list(const char * line, uint64_t size,
                    struct path_list_head * head, int dump_all);


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
 * Free buffers related to one path entry.
 *
 * Parameters:
 *     entry - The path list entry.
 *
 * Return: none
 *
 */
void free_path_entry(struct path_list_entry * entry);


/** ***************************************************************************
 * Inserts the first file in a path list into the next available slot.
 * Subsequent files of the same size are added with insert_end_path().
 *
 * Parameters:
 *    filename  - The filename of this first file.
 *    dir_entry - Directory containing filename.
 *    size      - Size of the file.
 *
 * Return: Pointer to the head of this path list.
 *
 */
struct path_list_head * insert_first_path(char * filename,
                                          struct direntry * dir_entry,
                                          uint64_t size);


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
 *    inode     - The inode of this path.
 *    size      - The size of the files in this path list.
 *    head      - The head of this path list (from insert_first_path()).
 *
 * Return: none
 *
 */
void insert_end_path(char * filename, struct direntry * dir_entry,
                     ino_t inode, uint64_t size, struct path_list_head * head);


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
 * Mark the given path list entry unique.
 *
 * Parameters:
 *    head  - Head of the path list containing entry.
 *    entry - The entry to mark invalid.
 *
 * Return: remaining size of this path list (may be down to zero)
 *
 */
void mark_path_entry_unique(struct path_list_head * head,
                            struct path_list_entry * entry);


/** ***************************************************************************
 * Mark the given path list entry as ignored. This is done when file cannot
 * be read or was truncated.
 *
 * Parameters:
 *    head  - Head of the path list containing entry.
 *    entry - The entry to mark invalid.
 *
 * Return: remaining size of this path list (may be down to zero)
 *
 */
int mark_path_entry_ignore(struct path_list_head * head,
                           struct path_list_entry * entry);


/** ***************************************************************************
 * Mark the given path list entry as ignored because it is a hardlink.
 * Only relevant when --hardlink-is-unique option is used.
 *
 * Parameters:
 *    head  - Head of the path list containing entry.
 *    entry - The entry to mark invalid.
 *
 * Return: remaining size of this path list (may be down to zero)
 *
 */
int mark_path_entry_ignore_hardlink(struct path_list_head * head,
                                    struct path_list_entry * entry);


/** ***************************************************************************
 * Mark the given path list entry ready for hashing.
 * If all entries in this path list are now ready, mark the path list ready.
 *
 * Parameters:
 *    head  - Head of the path list containing entry.
 *    entry - The entry to mark invalid.
 *
 * Return: none
 *
 */
void mark_path_entry_ready(struct path_list_head * head,
                           struct path_list_entry * entry);


#endif
