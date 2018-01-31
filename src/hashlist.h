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

#ifndef _DUPD_HASHLISTS_H
#define _DUPD_HASHLISTS_H

#include <sqlite3.h>
#include <stdint.h>
#include <sys/types.h>

#include "hash.h"
#include "paths.h"

struct hash_list;


/** ***************************************************************************
 * Free all entries in this hash list.
 *
 * Parameters:
 *    hl - Pointer to the head of the list to free.
 *
 * Return: none
 *
 */
void free_hash_list(struct hash_list * hl);


/** ***************************************************************************
 * Create a new hash list, empty but initialized. The new list will
 * have the number of nodes defined by the current defines.
 *
 * Caller is responsible for calling free_hash_list() on the returned
 * hash list, as these are not freed by free_hash_lists().
 *
 * Parameters: none
 *
 * Return: An allocated hash list, empty of data.
 *
 */
struct hash_list * init_hash_list();


/** ***************************************************************************
 * Reset a hash list node so it is empty of data but keeps all its
 * allocated space. This allows reusing the same hash list for new
 * data so we don't have to allocate a new one.
 *
 * Parameters:
 *    hl - Pointer to the head of the list to reset.
 *
 * Return: none
 *
 */
void reset_hash_list(struct hash_list * hl);


/** ***************************************************************************
 * Adds a new file (path) to a given hash list. The hash list capacity gets
 * expanded if necessary to hold the new file.
 *
 * Parameters:
 *     hl   - Add file to this hash list.
 *     file - File to add.
 *     hash - The hash of this file (full or partial depending on round).
 *
 * Return: none.
 *
 */
void add_to_hash_list(struct hash_list * hl,
                      struct path_list_entry * file, char * hash);


/** ***************************************************************************
 * Adds a new file to a given hash list. The hash of the file
 * will be computed by reading 'blocks' number of blocks from the
 * file.  The hash list capacity gets expanded if necessary to hold
 * the new file.
 *
 * Parameters:
 *     hl     - Add file to this hash list.
 *     file - File to add.
 *     blocks - Number of blocks to read from file path when computing
 *              its hash. If 0, reads entire file.
 *     bsize  - Size of blocks to read.
 *     skip   - Skip this many blocks when hashing from the file.
 *
 * Return: none.
 *
 */
void add_hash_list(struct hash_list * hl, struct path_list_entry * file,
                   uint64_t blocks, int bsize, off_t skip);


/** ***************************************************************************
 * Adds a new file to a given hash list. The hash of the file
 * will be computed from the buffer given.
 * The hash list capacity gets expanded if necessary to hold the new file.
 *
 * Parameters:
 *     hl      - Add file to this hash list.
 *     file - File to add.
 *     buffer  - Read file data from this buffer.
 *     bufsize - Size of buffer.
 *
 * Return: none.
 *
 */
void add_hash_list_from_mem(struct hash_list * hl,
                            struct path_list_entry * file,
                            const char * buffer, off_t bufsize);


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
 *     dbh   - Database handle.
 *     hl    - Source hash list.
 *     size  - The size of these files.
 *     round - Round in which these duplicates are being published.
 *
 * Return: none.
 *
 */
void publish_duplicate_hash_list(sqlite3 * dbh,
                                 struct hash_list * hl, off_t size, int round);


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
 * Look for unique files identified in the given hash list.
 *
 * The path of unique entries is nulled out (in its path list, given that the
 * hash list path is a pointer to the path list).
 *
 * If record_in_db is true, these files are also saved in the database.
 *
 * Parameters:
 *     dbh - Database handle.
 *     src - Source hash list.
 *
 * Return: The number of skimmed (nulled out) entries.
 *
 */
int skim_uniques(sqlite3 * dbh, struct hash_list * src, int record_in_db);


/** ***************************************************************************
 * Return true if this hash list has duplicates.
 *
 * Parameters:
 *     hl - Hash list.
 *
 * Return: 1 if hl has duplicates, 0 otherwise.
 *
 */
int hash_list_has_dups(struct hash_list * hl);


#endif
