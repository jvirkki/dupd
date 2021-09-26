/*
  Copyright 2017-2018 Jyri J. Virkki <jyri@virkki.com>

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dirtree.h"
#include "main.h"
#include "stats.h"
#include "utils.h"

struct dirbuf_list {
  char * ptr;
  char * pos;
  uint32_t size;
  uint32_t free;
  struct dirbuf_list * next;
};

static struct dirbuf_list * first_dirbuf;
static struct dirbuf_list * last_dirbuf;
static int direntry_size = sizeof(struct direntry);
static int total_dirbufs = 0;


/** ***************************************************************************
 * Return a pointer to 'size' bytes in the dirbuf buffers.
 *
 * A new dirbuf is allocated if not enough space is available.
 *
 * Parameters:
 *     size - Size (bytes) of the space requested.
 *
 * Return: Pointer to allocated space.
 *
 */
static inline char * dirbuf_alloc(uint32_t size)
{
  if (last_dirbuf->free < size) {
    uint32_t nextsize = last_dirbuf->size * 2;
    struct dirbuf_list * n;

    n = (struct dirbuf_list *)malloc(sizeof(struct dirbuf_list));
    last_dirbuf->next = n;
    last_dirbuf = n;

    last_dirbuf->ptr = (char *)malloc(nextsize);
    last_dirbuf->pos = last_dirbuf->ptr;
    last_dirbuf->size = nextsize;
    last_dirbuf->free = nextsize;
    last_dirbuf->next = NULL;

    inc_stats_dirbuf(nextsize);
  }

  char * p = last_dirbuf->pos;
  last_dirbuf->pos += size;
  last_dirbuf->free -= size;
  total_dirbufs += size;

  return p;
}


/** ***************************************************************************
 * Public function, see dirtree.h
 *
 */
void init_dirtree()
{
  uint32_t init_size = K4;

  if (x_small_buffers) { init_size = 128; }
  first_dirbuf = (struct dirbuf_list *)malloc(sizeof(struct dirbuf_list));
  first_dirbuf->ptr = (char *)malloc(init_size);
  inc_stats_dirbuf(init_size);

  last_dirbuf = first_dirbuf;
  last_dirbuf->pos = last_dirbuf->ptr;
  last_dirbuf->size = init_size;
  last_dirbuf->free = init_size;
  last_dirbuf->next = NULL;
}


/** ***************************************************************************
 * Public function, see dirtree.h
 *
 */
void free_dirtree()
{
  struct dirbuf_list * p;
  struct dirbuf_list * prev;
  uint64_t total = 0;

  p = first_dirbuf;
  while(p != NULL) {
    free(p->ptr);
    total += p->size;
    prev = p;
    p = p->next;
    free(prev);
  }

  first_dirbuf = NULL;
  last_dirbuf = NULL;
  dec_stats_dirbuf(total);
}


/** ***************************************************************************
 * Public function, see dirtree.h
 *
 */
struct direntry * new_child_dir(char * name, struct direntry * parent)
{
  struct direntry * entry = (struct direntry *)dirbuf_alloc(direntry_size);

  uint8_t len = (uint8_t)strlen(name);

  if (len == 1 && name[0] == '/') {
    if (parent != NULL) {
      printf("error: new_child_dir: / has non-null parent\n");
      exit(1);
    }
    len = 0;
    entry->name_size = 0;
    entry->name = NULL;
  } else {
    entry->name_size = len;
    entry->name = dirbuf_alloc(len);
    memcpy(entry->name, name, len);
  }

  entry->parent = parent;

  if (parent == NULL) {
    entry->total_size = len;
  } else {
    entry->total_size = len + 1 + parent->total_size;
  }

  return entry;
}


/** ***************************************************************************
 * Completes filling the buffer with all path components.
 *
 * Parameters:
 *    filename - Pointer to the filename (may or may not be NULL-terminated).
 *    name_len - Length of filename.
 *    pos      - Start writing at this position (but we go backwards).
 *    buffer - Buffer for writing output, was allocated by caller.
 *    dir    - Start building path from this directory.
 *
 * Return: none (fills buffer)
 *
 */
static void internal_build_path(char * filename, int name_len,
                                uint16_t pos, char * buffer,
                                struct direntry * dir)
{
  // First copy the filename to the end of the path buffer
  buffer[pos] = '/';
  memcpy(buffer + pos + 1, filename, name_len);
  buffer[pos + name_len + 1] = 0;

  // Then walk up the tree filling parent directory name until done
  while (dir != NULL) {
    if (dir->name_size > 0) {
      pos -= dir->name_size;
      memcpy(buffer + pos, dir->name, dir->name_size);
      pos--;
    }
    dir = dir->parent;
    if (dir != NULL) { buffer[pos] = '/'; }
  }
}


/** ***************************************************************************
 * Public function, see dirtree.h
 *
 */
void build_path_from_string(char * filename, struct direntry * entry,
                            char * buffer)
{
  int name_len = strlen(filename);
  uint16_t pos = entry->total_size;

  internal_build_path(filename, name_len, pos, buffer, entry);
}


/** ***************************************************************************
 * Public function, see dirtree.h
 *
 */

void build_path(struct path_list_entry * entry, char * buffer)
{
  int name_len = entry->filename_size;
  uint16_t pos = entry->dir->total_size;
  char * filename = pb_get_filename(entry);

  internal_build_path(filename, name_len, pos, buffer, entry->dir);
}
