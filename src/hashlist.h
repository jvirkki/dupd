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

#ifndef _DUPD_HASHLISTS_H
#define _DUPD_HASHLISTS_H

#include <sqlite3.h>
#include <sys/types.h>

#define HASH_LIST_ONE 1
#define HASH_LIST_PARTIAL 2
#define HASH_LIST_FULL 3


/** ***************************************************************************
 * A hash list node. See new_hash_list_node().
 *
 */
struct hash_list {
  int has_dups;                 // true if this hash list has duplicates
  int hash_valid;               // true if hash buffer is set to valid value
  char hash[16];                // the hash string shared by all these paths
  char * paths;                 // all the paths with a given hash
  int capacity;                 // 'paths' block current capacity
  int next_index;               // when adding a path, index of next one
  struct hash_list * next;      // next in list
};

#define HASH_LIST_HAS_DUPS(hl) (hl->has_dups)
#define HASH_LIST_NO_DUPS(hl) (!(hl->has_dups))


/** ***************************************************************************
 * Initialize the hash lists needed by dupd. After initialization, the
 * lists can be retrieved with get_hash_list().
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void init_hash_lists();


/** ***************************************************************************
 * Retrieve a pointer to one of the available hash lists. Use one of
 * the HASH_LIST_* constants defined above to name the kind of list to
 * retrieve.
 *
 * Parameters:
 *     kind - The specific hash list to return.
 *
 * Return: Pointer to the head of the requested hash list. The list is
 * allocated but empty. Can return NULL if 'kind' is invalid.
 *
 */
struct hash_list * get_hash_list(int kind);


/** ***************************************************************************
 * Adds a new file (path) to a given hash list. The hash of the file
 * will be computed by reading 'blocks' number of blocks from the
 * file.  The hash list capacity gets expanded if necessary to hold
 * the new file.
 *
 * Parameters:
 *     hl     - Add file to this hash list.
 *     path   - Path of the file to add.
 *     blocks - Number of blocks to read from file path when computing
 *              its hash. If 0, reads entire file.
 *     skip   - Skip this many blocks when hashing from the file.
 *
 * Return: none.
 *
 */
void add_hash_list(struct hash_list * hl, char * path, int blocks, int skip);


/** ***************************************************************************
 * Copy potentially duplicate files from src hash list to destination hash
 * list, recomputing the hash using 'blocks' blocks.
 *
 * Files which do not have potential duplicates in the src list are dropped,
 * so in most cases destination hash list will contain fewer files than src.
 *
 * Parameters:
 *     src         - Source hash list.
 *     blocks      - Number of blocks to read from file path when computing
 *                   its hash. If 0, reads entire file.
 *     destination - Destination hash list.
 *     skip        - Skip this many blocks when hashing from the file.
 *
 * Return: none.
 *
 */
void filter_hash_list(struct hash_list * src, int blocks,
                      struct hash_list * destination, int skip);


/** ***************************************************************************
 * Export all duplicate files from the given hash list to the duplicates
 * table in the database.
 *
 * This should only be called on a hash list which has been built from
 * full file hashes so the duplicates are known to be real duplicates.
 *
 * If write_db is false, only print the files to stdout.
 *
 * Parameters:
 *     dbh  - Database handle.
 *     hl   - Source hash list.
 *     size - The size of these files.
 *
 * Return: none.
 *
 */
void publish_duplicate_hash_list(sqlite3 * dbh,
                                 struct hash_list * hl, off_t size);


/** ***************************************************************************
 * Print to stdout the contents of a hash list.
 *
 * Parameters:
 *     src - Source hash list.
 *
 * Return: none.
 *
 */
void print_hash_list(struct hash_list * src);


/** ***************************************************************************
 * Export all unique files from the given hash list to the database.
 *
 * Parameters:
 *     dbh - Database handle.
 *     src - Source hash list.
 *
 * Return: none.
 *
 */
void record_uniques(sqlite3 * dbh, struct hash_list * src);


#endif
