/*
  Copyright 2012-2021 Jyri J. Virkki <jyri@virkki.com>

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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "dbops.h"
#include "dirtree.h"
#include "dtrace.h"
#include "hash.h"
#include "hashlist.h"
#include "hashlist_priv.h"
#include "main.h"
#include "paths.h"
#include "sizelist.h"
#include "stats.h"
#include "utils.h"

#define DEFAULT_PATH_CAPACITY 4
#define DEFAULT_HASHLIST_ENTRIES 6

struct path_buffer_info {
  int size;
  char * buf;
};



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

  hl->hash_valid = 0;
  hl->hash[0] = 0;

  hl->entries = (struct path_list_entry **)
    malloc(sizeof(struct path_list_entry *) * DEFAULT_PATH_CAPACITY);

  hl->capacity = DEFAULT_PATH_CAPACITY;
  hl->next_index = 0;
  hl->next = NULL;
  return hl;
}


/** ***************************************************************************
 * Reset a hash list entry.
 *
 */
static void reset_hash_list_entry(struct hash_list * hl)
{
  if (hl == NULL) {
    return;
  }

  hl->hash_valid = 0;
  hl->hash[0] = 0;
  hl->next_index = 0;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void add_to_hash_table(struct hash_table * hl,
                       struct path_list_entry * file, char * hash)
{
  struct hash_list * p = NULL;
  struct hash_list * tail = NULL;
  int hl_len = 0;
  uint8_t index;

  LOG_MORE_TRACE {
    char buffer[DUPD_PATH_MAX];
    build_path(file, buffer);
    LOG(L_MORE_TRACE, "Adding path %s to hash list\n", buffer);
  }

  index = hash[hash_bufsize-1];
  p = hl->table[index];
  if (p == NULL) {
    p = new_hash_list_node();
    hl->table[index] = p;
  }
  tail = p;

  // Find the node which contains the paths for this hash, if it exists.

  while (p != NULL && p->hash_valid) {
    hl_len++;
    if (!dupd_memcmp(p->hash, hash, hash_bufsize)) {

      if (p->next_index == p->capacity) {
        // Found correct node but need more space in path list
        p->capacity = p->capacity * 2;
        p->entries = (struct path_list_entry **)
          realloc(p->entries, p->capacity * sizeof(struct path_list_entry *));

        stats_hashlist_path_realloc++;
        LOG(L_RESOURCES, "Increased path capacity to %d\n", p->capacity);
      }

      // Add new path to existing node
      p->entries[p->next_index] = file;
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
    struct hash_list * new_node = new_hash_list_node();
    tail->next = new_node;
    p = new_node;
    stats_hash_list_len_inc++;
    LOG(L_RESOURCES, "Increased hash node list length to %d\n",
        hl_len + DEFAULT_HASHLIST_ENTRIES);
  }

  // Populate new node...
  memcpy(p->hash, hash, hash_bufsize);
  p->hash_valid = 1;
  p->entries[p->next_index] = file;
  p->next_index++;

  // If there are additional hash list entries beyond this one (from a prior
  // run) mark the next one invalid because it likely contains stale data.
  p = p->next;
  if (p != NULL) {
    reset_hash_list_entry(p);
  }

  return;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
struct hash_table * init_hash_table()
{
  struct hash_table * hl = NULL;

  hl = (struct hash_table *)malloc(sizeof(struct hash_table));
  hl->has_dups = 0;
  hl->entries = 256;

  struct hash_list ** hll = malloc(256 * sizeof(struct hash_list *));

  for (int n = 0; n <= 255; n++) {
    hll[n] = NULL;
  }

  hl->table = hll;

  return hl;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void reset_hash_table(struct hash_table * hl)
{
  if (hl == NULL) {
    return;
  }

  hl->has_dups = 0;

  for (int n = 0; n <= 255; n++) {
    reset_hash_list_entry(hl->table[n]);
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void free_hash_table(struct hash_table * hl)
{
  struct hash_list * me;
  struct hash_list * p;

  for (int n = 0; n <= 255; n++) {
    p = hl->table[n];
    me = p;
    while (p != NULL) {
      p = p->next;
      free(me->entries);
      free(me);
      me = p;
    }
    hl->table[n] = NULL;
  }
  free(hl->table);
  free(hl);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void add_hash_table(struct hash_table * hl, struct path_list_entry * file,
                    uint64_t blocks, int bsize, uint64_t skip)
{
  char hash_out[HASH_MAX_BUFSIZE];
  char path[DUPD_PATH_MAX];

  build_path(file, path);

  int rv = hash_fn(path, hash_out, blocks, bsize, skip);
  if (rv != 0) {
    LOG(L_SKIPPED, "SKIP [%s]: Unable to compute hash\n", path);
    return;
  }

  add_to_hash_table(hl, file, hash_out);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void add_hash_table_from_mem(struct hash_table * hl,
                             struct path_list_entry * file,
                             const char * buffer, uint32_t bufsize)
{
  char hash_out[HASH_MAX_BUFSIZE];
  hash_fn_buf(buffer, bufsize, hash_out);
  add_to_hash_table(hl, file, hash_out);
}


/** ***************************************************************************
 * Retrieve the thread-local path buffer.
 *
 * If not allocated yet, first do so.
 *
 */
static struct path_buffer_info * get_path_buffer_info()
{
  struct path_buffer_info * pbi = pthread_getspecific(duplicate_path_buffer);

  if (pbi == NULL) {
    pbi = (struct path_buffer_info *)malloc(sizeof(struct path_buffer_info));
    pbi->size = DUPD_PATH_MAX * 4;
    pbi->buf = (char *)malloc(pbi->size);
    pthread_setspecific(duplicate_path_buffer, pbi);
  }

  return pbi;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void free_path_buffer()
{
  struct path_buffer_info * pbi = pthread_getspecific(duplicate_path_buffer);

  if (pbi != NULL) {
    pthread_setspecific(duplicate_path_buffer, NULL);
    free(pbi->buf);
    free(pbi);
  }
}


/** ***************************************************************************
 * Publish a hash list to the database (see publish_duplicate_hash_table()).
 *
 */
static void publish_duplicate_hash_list(sqlite3 * dbh,
                                        struct hash_list * hl, uint64_t size)
{
  struct hash_list * p = hl;
  struct path_list_entry * entry;
  char file[DUPD_PATH_MAX];

  struct path_buffer_info * pbi = get_path_buffer_info();

  while (p != NULL && p->hash_valid) {

    if (p->next_index > 1) {

      stats_duplicate_groups++;
      stats_duplicate_files += p->next_index;

      if (log_level >= L_TRACE) {
        printf("Duplicates: file size: %ld, count: [%d]\n",
               (long)size, p->next_index);
        for (int j=0; j < p->next_index; j++) {
          entry = *(p->entries + j);
          build_path(entry, file);
          printf(" %s\n", file);
        }
      }

      // print separated list of the paths into buffer
      int pos = 0;
      for (int i = 0; i < p->next_index; i++) {

        // if not enough space (conservatively) in path_buffer, increase
        if (pos + DUPD_PATH_MAX > pbi->size) {
          pbi->size += DUPD_PATH_MAX * 10;
          pbi->buf = (char *)realloc(pbi->buf, pbi->size);
          LOG(L_RESOURCES, "Increased path_buffer %d\n", pbi->size);
        }

        entry = *(p->entries + i);
        build_path(entry, file);

        if (i + 1 < p->next_index) {
          pos += sprintf(pbi->buf + pos, "%s%c", file, path_separator);
        } else{
          sprintf(pbi->buf + pos, "%s%c", file, 0);
        }

        LOG_MORE_INFO {
          int hsize = hash_get_bufsize(hash_function);
          char hash_out[HASH_MAX_BUFSIZE];
          hash_fn(file, hash_out, 0, 0, 0);
          if (memcmp(hash_out, p->hash, hsize)) {            // LCOV_EXCL_START
            printf("file [%s] ", file);
            memdump("hash", p->hash, hsize);
            printf("error: computed hash differs from hash! [%s]\n", file);
            memdump("hash", hash_out, hsize);
            exit(1);
          }                                                  // LCOV_EXCL_STOP
        }

        dtrace_set_state(file, size, entry->state, FS_DONE);
        entry->state = FS_DONE;
        free_path_entry(file, size, entry);
      }

      // go publish to db
      duplicate_to_db(dbh, p->next_index, size, pbi->buf);

    }
    p = p->next;
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void publish_duplicate_hash_table(sqlite3 * dbh,
                                  struct hash_table * hl, uint64_t size)
{
  for (int n = 0; n <= 255; n++) {
    if (hl->table[n] != NULL) {
      publish_duplicate_hash_list(dbh, hl->table[n], size);
    }
  }
}


/** ***************************************************************************
 * Print a hash list for diagnostics.
 *
 */
static void print_hash_list_entry(int n, struct hash_list * src)
{
  char file[DUPD_PATH_MAX];
  struct path_list_entry * entry;
  struct hash_list * p = src;
  int header = 0;

  while (p != NULL && p->hash_valid) {
    if (!header) {
      LOG(L_TRACE, "  ---[ %d ]\n", n);
      header = 1;
    }
    LOG(L_TRACE, "hash_valid: %d, next_index: %d   ",
        p->hash_valid, p->next_index);
    LOG_TRACE {
      memdump("hash", p->hash, hash_bufsize);
    }
    for (int j=0; j < p->next_index; j++) {
      entry = *(p->entries + j);
      build_path(entry, file);
      LOG(L_TRACE, "  [%s]\n", file);
    }
    p = p->next;
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void print_hash_table(struct hash_table * src)
{
  LOG(L_TRACE, "=====hash_table at %p, has_dups: %d\n",
      src, src->has_dups);

  for (int n = 0; n < 256; n++) {
    if (src->table[n] != NULL) {
      print_hash_list_entry(n, src->table[n]);
    }
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int skim_uniques(struct path_list_head * head, struct hash_table * src)
{
  struct path_list_entry * entry;
  int skimmed = 0;
  struct hash_list * p;

  if (debug_size == head->sizelist->size) {
    dump_path_list("ENTER skim_uniques", head->sizelist->size, head, 1);
  }

  for (int n = 0; n <= 255; n++) {

    if (src->table[n] != NULL) {

      p = src->table[n];
      while (p != NULL && p->hash_valid) {

        if (debug_size == head->sizelist->size) {
          dump_path_list("skim_uniques", head->sizelist->size, head, 1);
        }

        // If this list has only one entry, it was unique.
        if (p->next_index == 1) {
          entry = *(p->entries);
          LOG(L_TRACE, "skim_uniques: marking a single entry list unique\n");
          mark_path_entry_unique(head, entry);
          skimmed++;
          increase_unique_counter(1);
        }
        p = p->next;
      }
    }
  }

  return skimmed;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
inline int hash_table_has_dups(struct hash_table * hl)
{
  return hl->has_dups;
}
