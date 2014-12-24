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
#include "md5.h"
#include "stats.h"

#define DEFAULT_PATH_CAPACITY 10
#define DEFAULT_HASH_DEPTH 6

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
  hl->paths = (char *)malloc(PATH_MAX * DEFAULT_PATH_CAPACITY);
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
  for (int n = 1; n < DEFAULT_HASH_DEPTH; n++) {
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
 *    hl - Pointer to the head of the list to reset.
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
    free(me->paths);
    free(me);
    me = p;
  }
}


/** ***************************************************************************
 * Reset a hash list so it is empty of data but keeps all its
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
  struct hash_list * p = hl;
  while (p != NULL) {
    p->has_dups = 0;
    p->hash_valid = 0;
    p->hash[0] = 0;
    p->next_index = 0;
    p = p->next;
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void init_hash_lists()
{
  assert(hl_one == NULL);
  assert(hl_partial == NULL);
  assert(hl_full == NULL);

  hl_one = init_hash_list();
  hl_partial = init_hash_list();
  hl_full = init_hash_list();
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
    return hl_one;
  case HASH_LIST_PARTIAL:
    reset_hash_list(hl_partial);
    return hl_partial;
  case HASH_LIST_FULL:
    reset_hash_list(hl_full);
    return hl_full;
  default:
    return NULL;
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void add_hash_list(struct hash_list * hl, char * path, int blocks, int skip)
{
  assert(hl != NULL);
  assert(path != NULL);

  char md5out[16];
  int rv = md5(path, md5out, blocks, skip);
  if (rv != 0) {
    if (verbosity >= 1) {
      printf("SKIP [%s]: Unable to compute hash\n", path);
      return;
    }
  }

  struct hash_list * p = hl;
  struct hash_list * tail = hl;

  // Find the node which contains the paths for this hash, if it exists.

  while (p != NULL && p->hash_valid) {
    if (!memcmp(p->hash, md5out, 16)) {

      if (p->next_index == p->capacity) {
        // Found correct node but need more space in path list
        p->capacity = p->capacity * 2;
        p->paths = (char *)realloc(p->paths, p->capacity * PATH_MAX);
        if (verbosity >= 5) {
          printf("Had to increase path capacity to %d\n", p->capacity);
        }
      }

      // Add new path to existing node
      strcpy(p->paths + p->next_index * PATH_MAX, path);
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
    if (verbosity >= 5) {
      printf("Had to increase hash node list length!\n");
    }
  }

  // Populate new node...
  memcpy(p->hash, md5out, 16);
  p->hash_valid = 1;
  strcpy(p->paths + p->next_index * PATH_MAX, path);
  p->next_index++;
  return;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void filter_hash_list(struct hash_list * src, int blocks,
                      struct hash_list * destination, int skip)
{
  struct hash_list * p = src;
  while (p != NULL) {

    if (p->hash_valid && (p->next_index > 1)) {
      // have two or more files with same hash here.. might be duplicates...
      // promote them to new hash list
      for (int j=0; j < p->next_index; j++) {
        add_hash_list(destination, p->paths + j * PATH_MAX, blocks, skip);
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
  while (p != NULL) {

    if (p->hash_valid && (p->next_index > 1)) {

      stats_duplicate_sets++;
      stats_duplicate_files += p->next_index;

      if (!write_db || verbosity >= 4) {
        printf("Duplicates: file size: %ld, count: [%d]\n",
               (long)size, p->next_index);
        for (int j=0; j < p->next_index; j++) {
          printf(" %s\n", p->paths + j * PATH_MAX);
        }
      }

      if (write_db) {
        if (path_buffer == NULL) {
          path_buffer = (char *)malloc((p->next_index + 2) * PATH_MAX);
          path_buffer_size = p->next_index + 2;
        } else {
          if (p->next_index > path_buffer_size) {
            path_buffer_size *= 2;
            if (p->next_index > path_buffer_size) {
              path_buffer_size = p->next_index + 2;
            }
            path_buffer = (char *)realloc(path_buffer,
                                          path_buffer_size * PATH_MAX);
            if (verbosity >= 5) {
              printf("Had to increase path_buffer to %d\n", path_buffer_size);
            }
          }
        }

        // print comma-separated list of the paths into buffer
        int pos = 0;
        for (int i = 0; i < p->next_index; i++) {
          if (i + 1 < p->next_index) {
            pos += sprintf(path_buffer + pos, "%s,", p->paths + i * PATH_MAX);
          } else{
            sprintf(path_buffer + pos, "%s%c", p->paths + i * PATH_MAX, 0);
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
  while (p != NULL) {
    printf("hash_valid: %d, has_dups: %d, next_index: %d\n",
           p->hash_valid, p->has_dups, p->next_index);
    for (int j=0; j < p->next_index; j++) {
      printf("  [%s]\n", p->paths + j * PATH_MAX);
    }
    p = p->next;
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void record_uniques(sqlite3 * dbh, struct hash_list * src)
{
  struct hash_list * p = src;
  while (p != NULL) {
    if (p->hash_valid && (p->next_index == 1)) {
      unique_to_db(dbh, p->paths, "hashlist");
    }
    p = p->next;
  }
}
