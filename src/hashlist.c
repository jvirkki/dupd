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

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "dbops.h"
#include "hashlist.h"
#include "main.h"
#include "hash.h"
#include "stats.h"
#include "utils.h"

#define DEFAULT_PATH_CAPACITY 4
#define DEFAULT_PATH_BUFFER 10
#define DEFAULT_HASHLIST_ENTRIES 6

static char * path_buffer = NULL;
static int path_buffer_size = 0;


/** ***************************************************************************
 * The three supported hash lists. Must be initialized with
 * initialize_hash_lists(). see header file for constants.
 *
 */
static struct hash_list * hl_one;
static struct hash_list * hl_partial;
static struct hash_list * hl_full;


/** ***************************************************************************
 * Create a new hash list node. A node can contains all the known
 * files (paths) which have a given hash. The new node returned is not
 * yet linked to any hash list.  The new node gets a preallocated path
 * capacity.
 *
 * Parameters: none
 *
 * Return: An initialized/allocated hash list node, empty of data.
 *
 */
static struct hash_list * new_hash_list_node()
{
  struct hash_list * hl = (struct hash_list *)malloc(sizeof(struct hash_list));
  hl->has_dups = 0;
  hl->hash_valid = 0;
  hl->hash[0] = 0;
  hl->pathptrs = (char **)malloc(sizeof(char *) * DEFAULT_PATH_CAPACITY);
  hl->capacity = DEFAULT_PATH_CAPACITY;
  hl->next_index = 0;
  hl->next = NULL;
  return hl;
}


/** ***************************************************************************
 * Create a new hash list, empty but initialized. The new list will
 * have the number of nodes defined by the current defines.
 *
 * Parameters: none
 *
 * Return: An allocated hash list, empty of data.
 *
 */
static struct hash_list * init_hash_list()
{
  struct hash_list * head = new_hash_list_node();
  struct hash_list * p = head;
  int entries = x_small_buffers ? 1 : DEFAULT_HASHLIST_ENTRIES;
  for (int n = 1; n < entries; n++) {
    struct hash_list * next_node = new_hash_list_node();
    p->next = next_node;
    p = next_node;
  }
  return head;
}


/** ***************************************************************************
 * Free all entries in this hash list.
 *
 * Parameters:
 *    hl - Pointer to the head of the list to free.
 *
 * Return: none
 *
 */
static void free_hash_list(struct hash_list * hl)
{
  struct hash_list * p = hl;
  struct hash_list * me = hl;
  while (p != NULL) {
    p = p->next;
    free(me->pathptrs);
    free(me);
    me = p;
  }
}


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
static void reset_hash_list(struct hash_list * hl)
{
  if (hl == NULL) {
    return;
  }

  hl->has_dups = 0;
  hl->hash_valid = 0;
  hl->hash[0] = 0;
  hl->next_index = 0;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void add_to_hash_list(struct hash_list * hl, char * path, char * hash)
{
  struct hash_list * p = hl;
  struct hash_list * tail = hl;
  int hl_len = 0;

  LOG_MORE_TRACE {
    LOG(L_MORE_TRACE, "Adding path %s to hash list which contains:\n", path);
    print_hash_list(hl);
  }

  // Find the node which contains the paths for this hash, if it exists.

  while (p != NULL && p->hash_valid) {
    hl_len++;
    if (!dupd_memcmp(p->hash, hash, hash_bufsize)) {

      if (p->next_index == p->capacity) {
        // Found correct node but need more space in path list
        p->capacity = p->capacity * 2;
        p->pathptrs =
          (char **)realloc(p->pathptrs, p->capacity * sizeof(char *));

        hashlist_path_realloc++;
        LOG(L_RESOURCES, "Increased path capacity to %d\n", p->capacity);
      }

      // Add new path to existing node
      p->pathptrs[p->next_index] = path;
      if (p->next_index) {
        hl->has_dups = 1;
      }
      p->next_index++;
      return;
    }
    tail = p;
    p = p->next;
  }

  // Got here if no hash match found (first time we see this hash).

  // If we don't have a unused node available, need to add a new node to list
  if (p == NULL) {
    struct hash_list * new_node = init_hash_list();
    tail->next = new_node;
    p = new_node;
    hash_list_len_inc++;
    LOG(L_RESOURCES, "Increased hash node list length to %d\n",
        hl_len + DEFAULT_HASHLIST_ENTRIES);
  }

  // Populate new node...
  memcpy(p->hash, hash, hash_bufsize);
  p->hash_valid = 1;
  p->pathptrs[p->next_index] = path;
  p->next_index++;

  // If there are additional hash list entries beyond this one (from a prior
  // run) mark the next one invalid because it likely contains stale data.
  p = p->next;
  if (p != NULL) {
    reset_hash_list(p);
  }

  return;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void init_hash_lists()
{
  assert(hl_one == NULL);                                    // LCOV_EXCL_LINE
  assert(hl_partial == NULL);                                // LCOV_EXCL_LINE
  assert(hl_full == NULL);                                   // LCOV_EXCL_LINE

  hl_one = init_hash_list();
  hl_full = init_hash_list();

  // The intermediate hash list is unused by default, so only allocate it
  // if it has been enabled.
  if (intermediate_blocks > 0) {
    hl_partial = init_hash_list();
  }

  if (x_small_buffers) {
    path_buffer = (char *)malloc(10);
    path_buffer_size = 10;
  } else {
    path_buffer = (char *)malloc(DEFAULT_PATH_BUFFER * DUPD_PATH_MAX);
    path_buffer_size = DEFAULT_PATH_BUFFER * DUPD_PATH_MAX;
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void free_hash_lists()
{
  if (hl_one != NULL) {
    free_hash_list(hl_one);
  }
  if (hl_partial != NULL) {
    free_hash_list(hl_partial);
  }
  if (hl_full != NULL) {
    free_hash_list(hl_full);
  }
  if (path_buffer != NULL) {
    free(path_buffer);
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
struct hash_list * get_hash_list(int kind)
{
  switch (kind) {
  case HASH_LIST_ONE:
    reset_hash_list(hl_one);
    reset_hash_list(hl_one->next);
    return hl_one;
  case HASH_LIST_PARTIAL:
    reset_hash_list(hl_partial);
    reset_hash_list(hl_partial->next);
    return hl_partial;
  case HASH_LIST_FULL:
    reset_hash_list(hl_full);
    reset_hash_list(hl_full->next);
    return hl_full;
  default:
    return NULL;
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void add_hash_list(struct hash_list * hl, char * path, uint64_t blocks,
                   int bsize, off_t skip)
{
  assert(hl != NULL);                                        // LCOV_EXCL_LINE
  assert(path != NULL);                                      // LCOV_EXCL_LINE

  char hash_out[HASH_MAX_BUFSIZE];
  int rv = hash_fn(path, hash_out, blocks, bsize, skip);
  if (rv != 0) {
    LOG(L_SKIPPED, "SKIP [%s]: Unable to compute hash\n", path);
    return;
  }

  add_to_hash_list(hl, path, hash_out);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void add_hash_list_from_mem(struct hash_list * hl, char * path,
                            const char * buffer, off_t bufsize)

{
  assert(hl != NULL);                                        // LCOV_EXCL_LINE
  assert(path != NULL);                                      // LCOV_EXCL_LINE

  char hash_out[HASH_MAX_BUFSIZE];
  hash_fn_buf(buffer, bufsize, hash_out);
  add_to_hash_list(hl, path, hash_out);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void filter_hash_list(struct hash_list * src, uint64_t blocks, int bsize,
                      struct hash_list * destination, off_t skip)
{
  struct hash_list * p = src;
  while (p != NULL && p->hash_valid) {

    if (p->next_index > 1) {
      // have two or more files with same hash here.. might be duplicates...
      // promote them to new hash list
      for (int j=0; j < p->next_index; j++) {
        add_hash_list(destination, *(p->pathptrs + j), blocks, bsize, skip);
      }
    }
    p = p->next;
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void publish_duplicate_hash_list(sqlite3 * dbh,
                                 struct hash_list * hl, off_t size)
{
  struct hash_list * p = hl;
  while (p != NULL && p->hash_valid) {

    if (p->next_index > 1) {

      stats_duplicate_sets++;
      stats_duplicate_files += p->next_index;

      if (!write_db || log_level >= L_TRACE) {
        printf("Duplicates: file size: %ld, count: [%d]\n",
               (long)size, p->next_index);
        for (int j=0; j < p->next_index; j++) {
          printf(" %s\n", *(p->pathptrs + j));
        }
      }

      if (write_db) {
        // print separated list of the paths into buffer
        int pos = 0;
        for (int i = 0; i < p->next_index; i++) {

          // if not enough space (conservatively) in path_buffer, increase
          if (pos + DUPD_PATH_MAX > path_buffer_size) {
            path_buffer_size += DUPD_PATH_MAX * 10;
            path_buffer = (char *)realloc(path_buffer, path_buffer_size);
            path_buffer_realloc++;
            LOG(L_RESOURCES, "Increased path_buffer %d\n", path_buffer_size);
          }

          if (i + 1 < p->next_index) {
            pos += sprintf(path_buffer + pos,
                           "%s%c", *(p->pathptrs + i), path_separator);
          } else{
            sprintf(path_buffer + pos, "%s%c", *(p->pathptrs + i), 0);
          }
        }

        // go publish to db
        duplicate_to_db(dbh, p->next_index, size, path_buffer);
      }
    }
    p = p->next;
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void print_hash_list(struct hash_list * src)
{
  struct hash_list * p = src;
  while (p != NULL && p->hash_valid) {
    LOG(L_TRACE, "hash_valid: %d, has_dups: %d, next_index: %d   ",
        p->hash_valid, p->has_dups, p->next_index);
    memdump("hash", p->hash, hash_bufsize);
    for (int j=0; j < p->next_index; j++) {
      LOG(L_TRACE, "  [%s]\n", *(p->pathptrs + j));
    }
    p = p->next;
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int skim_uniques(sqlite3 * dbh, struct hash_list * src, int record_in_db)
{
  int skimmed = 0;
  struct hash_list * p = src;
  while (p != NULL && p->hash_valid) {
    if (p->next_index == 1) {
      if (record_in_db) {
        unique_to_db(dbh, *(p->pathptrs), "hashlist");
      }
      *(p->pathptrs)[0] = 0;
      skimmed++;
    }
    p = p->next;
  }
  return skimmed;
}
