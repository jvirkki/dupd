/*
  Copyright 2016-2018 Jyri J. Virkki <jyri@virkki.com>

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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "dirtree.h"
#include "main.h"
#include "readlist.h"
#include "utils.h"

struct read_list_entry * read_list = NULL;
long read_list_end;
static long read_list_size;


/** ***************************************************************************
 * Sort function used by sort_read_list() when sorting by inode.
 *
 */
static int rl_compare_i(const void * a, const void * b)
{
  struct read_list_entry * f = (struct read_list_entry *)a;
  struct read_list_entry * s = (struct read_list_entry *)b;

  return f->inode - s->inode;
}


/** ***************************************************************************
 * Sort function used by sort_read_list() when sorting by block.
 *
 */
static int rl_compare_b(const void * a, const void * b)
{
  struct read_list_entry * f = (struct read_list_entry *)a;
  struct read_list_entry * s = (struct read_list_entry *)b;
#ifdef USE_FIEMAP
  if (f->block > s->block) { return 1; }
  if (f->block < s->block) { return -1; }
  return 0;
#else
  return f->inode - s->inode;
#endif
}


/** ***************************************************************************
 * Public function, see readlist.h
 *
 */
void init_read_list()
{
  read_list_size = file_count / 4;
  if (x_small_buffers) { read_list_size = 8; }

  read_list_end = 0;

  read_list = (struct read_list_entry *)calloc(read_list_size,
                                               sizeof(struct read_list_entry));
}


/** ***************************************************************************
 * Public function, see readlist.h
 *
 */
void free_read_list()
{
  if (read_list == NULL) {
    return;
  }

  free(read_list);
  read_list = NULL;
  read_list_size = 0;
  read_list_end = 0;
}


/** ***************************************************************************
 * Public function, see readlist.h
 *
 */
void add_to_read_list(uint64_t block, ino_t inode,
                      struct path_list_head * head,
                      struct path_list_entry * entry)
{
#ifdef USE_FIEMAP
  read_list[read_list_end].block = block;
#endif
  read_list[read_list_end].inode = inode;
  read_list[read_list_end].pathlist_head = head;
  read_list[read_list_end].pathlist_self = entry;

  read_list_end++;
  if (read_list_end == read_list_size) {
    read_list_size *= 2;
    read_list = (struct read_list_entry *)
      realloc(read_list, sizeof(struct read_list_entry) * read_list_size);
  }
}


/** ***************************************************************************
 * Public function, see readlist.h
 *
 */
void sort_read_list(int use_block)
{
  int sort_blocks = 0;
#ifdef USE_FIEMAP
  sort_blocks = 1;
#endif

  switch (sort_bypass) {
  case SORT_BY_NONE:
    return;
  case SORT_BY_INODE:
    qsort(read_list, read_list_end,
          sizeof(struct read_list_entry), rl_compare_i);
    return;
  case SORT_BY_BLOCK:
    if (sort_blocks) {
      qsort(read_list, read_list_end,
            sizeof(struct read_list_entry), rl_compare_b);
    }
    return;
  }

  if (!use_block) { sort_blocks = 0; }
  if (!hdd_mode) { sort_blocks = 0; }

  if (hardlink_is_unique) {

    // Have to sort by inode to purge duplicates
    qsort(read_list, read_list_end,
          sizeof(struct read_list_entry), rl_compare_i);

    // Now that the read_list is ordered by inode, remove any paths
    // which are duplicate inodes given that we don't care about them.
    long i;
    ino_t prev = 0;
    for (i = 0; i < read_list_end; i++) {
      if (read_list[i].inode == prev) {
        char path[DUPD_PATH_MAX];
        build_path(read_list[i].pathlist_self, path);
        LOG(L_SKIPPED, "Skipping [%s] due to duplicate inode.", path);
        read_list[i].pathlist_head->list_size--;
        read_list[i].pathlist_self->state = FS_INVALID;
#ifdef USE_FIEMAP
        read_list[i].block = 0;
#endif
      }
      prev = read_list[i].inode;
    }
  }

  if (!hardlink_is_unique && !sort_blocks && hdd_mode) {
    qsort(read_list, read_list_end,
          sizeof(struct read_list_entry), rl_compare_i);
  }

  if (sort_blocks) {
    qsort(read_list, read_list_end,
          sizeof(struct read_list_entry), rl_compare_b);
  }
}
