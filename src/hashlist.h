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
#include <stdio.h>
#include <sys/types.h>

#include "paths.h"

struct hash_table;


/** ***************************************************************************
 * Free all entries in this hash table.
 *
 * Parameters:
 *    hl - Pointer to the head of the table to free.
 *
 * Return: none
 *
 */
void free_hash_table(struct hash_table * hl);


/** ***************************************************************************
 * Free thread-local path buffer.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void free_path_buffer();


/** ***************************************************************************
 * Create a new hash table, empty but initialized.
 *
 * Caller is responsible for calling free_hash_table() when done.
 *
 * Parameters:
 *    none
 *
 * Return: An allocated hash table, empty of data.
 *
 */
struct hash_table * init_hash_table();


/** ***************************************************************************
 * Reset a hash table so it is empty of data but keeps all its
 * allocated space. This allows reusing the same hash table for new
 * data so we don't have to allocate a new one.
 *
 * Parameters:
 *    hl - Pointer to the head of the table to reset.
 *
 * Return: none
 *
 */
void reset_hash_table(struct hash_table * hl);


/** ***************************************************************************
 * Adds a new file (path) to a given hash table. The hash table capacity gets
 * expanded if necessary to hold the new file.
 *
 * Parameters:
 *     hl   - Add file to this hash table.
 *     file - File to add.
 *     hash - The hash of this file (full or partial depending on round).
 *
 * Return: none.
 *
 */
void add_to_hash_table(struct hash_table * hl,
                       struct path_list_entry * file, char * hash);


/** ***************************************************************************
 * Adds a new file to a given hash table. The hash of the file
 * will be computed by reading 'blocks' number of blocks from the
 * file.  The hash table capacity gets expanded if necessary to hold
 * the new file.
 *
 * Parameters:
 *     hl     - Add file to this hash table.
 *     file - File to add.
 *     blocks - Number of blocks to read from file path when computing
 *              its hash. If 0, reads entire file.
 *     bsize  - Size of blocks to read.
 *     skip   - Skip this many blocks when hashing from the file.
 *
 * Return: none.
 *
 */
void add_hash_table(struct hash_table * hl, struct path_list_entry * file,
                    uint64_t blocks, int bsize, uint64_t skip);


/** ***************************************************************************
 * Adds a new file to a given hash table. The hash of the file
 * will be computed from the buffer given.
 * The hash table capacity gets expanded if necessary to hold the new file.
 *
 * Parameters:
 *     hl      - Add file to this hash table.
 *     file - File to add.
 *     buffer  - Read file data from this buffer.
 *     bufsize - Size of buffer.
 *
 * Return: none.
 *
 */
void add_hash_table_from_mem(struct hash_table * hl,
                             struct path_list_entry * file,
                             const char * buffer, uint32_t bufsize);


/** ***************************************************************************
 * Export all duplicate files from the given hash table to the duplicates
 * table in the database.
 *
 * This should only be called on a hash table which has been built from
 * full file hashes so the duplicates are known to be real duplicates.
 *
 * If write_db is false, only print the files to stdout.
 *
 * Parameters:
 *     dbh   - Database handle.
 *     hl    - Source hash table.
 *     size  - The size of these files.
 *
 * Return: none.
 *
 */
void publish_duplicate_hash_table(sqlite3 * dbh, struct hash_table * hl,
                                  uint64_t size);


/** ***************************************************************************
 * Print to stdout the contents of a hash table.
 *
 * Parameters:
 *     src - Source hash table.
 *
 * Return: none.
 *
 */
void print_hash_table(struct hash_table * src);


/** ***************************************************************************
 * Look for unique files identified in the given hash table.
 *
 * The unique entries are marked invalid.
 *
 * If record_in_db is true, these files are also saved in the database.
 *
 * Parameters:
 *     head - The pathlist which was used to build the hashlist.
 *     src  - Source hash table.
 *
 * Return: The number of skimmed entries.
 *
 */
int skim_uniques(struct path_list_head * head, struct hash_table * src);


/** ***************************************************************************
 * Return true if this hash table has duplicates.
 *
 * Parameters:
 *     hl - Hash table.
 *
 * Return: 1 if hl has duplicates, 0 otherwise.
 *
 */
int hash_table_has_dups(struct hash_table * hl);


#endif
