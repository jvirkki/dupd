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
#include "stats.h"
#include "utils.h"

#define INITIAL_READ_LIST_SIZE 100000

struct read_list_entry * read_list = NULL;
long read_list_end;
static long read_list_size;

struct read_list_entry * inode_read_list = NULL;
static long inode_read_list_end;
static long inode_read_list_size;


/** ***************************************************************************
 * Dump read list.
 *
 */
                                                             // LCOV_EXCL_START
static void dump_read_list(int with_path_list)
{
  printf("--- dumping read_list ---\n");
  for (int i = 0; i < read_list_end; i++) {
    printf("[%d] inode: %8ld  %" PRIu64 "\n",
           i, (long)read_list[i].inode, read_list[i].block);
    if (with_path_list) {
      dump_path_list("---", 0, read_list[i].pathlist_head, 1);
    }
  }
}
                                                             // LCOV_EXCL_STOP


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

  if (f->block > s->block) { return 1; }
  if (f->block < s->block) { return -1; }
  return 0;
}


/** ***************************************************************************
 * Public function, see readlist.h
 *
 */
void init_read_list()
{
  read_list_size = INITIAL_READ_LIST_SIZE;

  if (x_small_buffers) { read_list_size = 8; }

  read_list_end = 0;

  read_list = (struct read_list_entry *)calloc(read_list_size,
                                               sizeof(struct read_list_entry));

  if (hardlink_is_unique) {
    inode_read_list_size = INITIAL_READ_LIST_SIZE;
    if (x_small_buffers) { inode_read_list_size = 8; }
    inode_read_list_end = 0;
    inode_read_list =
      (struct read_list_entry *)calloc(inode_read_list_size,
                                       sizeof(struct read_list_entry));
  }
}


/** ***************************************************************************
 * Public function, see readlist.h
 *
 */
void free_read_list()
{
  if (read_list != NULL) {
    free(read_list);
    read_list = NULL;
    read_list_size = 0;
    read_list_end = 0;
  }
}


/** ***************************************************************************
 * Free the inode_read_list.
 *
 */
static void free_inode_read_list()
{
  if (inode_read_list != NULL) {
    free(inode_read_list);
    inode_read_list = NULL;
    inode_read_list_size = 0;
    inode_read_list_end = 0;
  }
}


/** ***************************************************************************
 * Add a single entry to the read list.
 *
 */
static void add_one_to_read_list(struct path_list_head * head,
                                 struct path_list_entry * entry,
                                 ino_t inode, uint64_t block)
{
  read_list[read_list_end].pathlist_head = head;
  read_list[read_list_end].pathlist_self = entry;
  read_list[read_list_end].block = block;
  read_list[read_list_end].inode = inode;
  read_list[read_list_end].done = 0;

  read_list_end++;
  if (read_list_end == read_list_size) {
    read_list_size *= 2;
    read_list = (struct read_list_entry *)
      realloc(read_list, sizeof(struct read_list_entry) * read_list_size);
  }
}


/** ***************************************************************************
 * Add a single entry to the inode read list.
 *
 */
static void add_one_to_inode_read_list(struct path_list_head * head,
                                       struct path_list_entry * entry,
                                       ino_t inode)
{
  if (hardlink_is_unique) {
    inode_read_list[inode_read_list_end].pathlist_head = head;
    inode_read_list[inode_read_list_end].pathlist_self = entry;
    inode_read_list[inode_read_list_end].block = inode;
    inode_read_list[inode_read_list_end].inode = inode;
    inode_read_list[inode_read_list_end].done = 0;

    inode_read_list_end++;
    if (inode_read_list_end == inode_read_list_size) {
      inode_read_list_size *= 2;
      inode_read_list = (struct read_list_entry *)
        realloc(inode_read_list, sizeof(struct read_list_entry) *
                inode_read_list_size);
    }
  }
}


/** ***************************************************************************
 * Public function, see readlist.h
 *
 */
void add_to_read_list(struct path_list_head * head,
                      struct path_list_entry * entry, ino_t inode)
{
  if (entry->blocks == NULL) {                               // LCOV_EXCL_START
    printf("error: add_to_read_list but no block(s)\n");
    dump_path_list("block missing", 0, head, 1);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  // If using_fiemap, may add multiple entries for the same file, one for
  // each block. If not, there is only one "block", the inode.

  for (int b = 0; b < entry->blocks->count; b++) {
    add_one_to_read_list(head, entry, inode, entry->blocks->entry[b].block);
  }

  // When hardlink_is_unique, keep a separate inode_read_list which can
  // only have one entry per file. It might seem we can filter out
  // hardlinks by looking at blocks and that is usually true. However
  // (see get_block_info_from_path()) sometimes block can be zero even
  // if it isn't, so this may fail. Easiest reliable way is to keep this
  // separate inode list.
  if (hardlink_is_unique) {
    add_one_to_inode_read_list(head, entry, inode);
  }
}


/** ***************************************************************************
 * Public function, see readlist.h
 *
 */
void sort_read_list()
{
  switch (sort_bypass) {
  case SORT_BY_NONE:
    return;
  case SORT_BY_INODE:
    qsort(read_list, read_list_end,
          sizeof(struct read_list_entry), rl_compare_i);
    return;
  case SORT_BY_BLOCK:
    qsort(read_list, read_list_end,
          sizeof(struct read_list_entry), rl_compare_b);
    return;
  }

  if (hardlink_is_unique) {

    qsort(inode_read_list, inode_read_list_end,
          sizeof(struct read_list_entry), rl_compare_i);

    // Now that the inode_read_list is ordered, remove any paths which
    // are duplicate inode given that we don't care about them.

    char path[DUPD_PATH_MAX];
    long i;
    ino_t p_inode = 0;

    for (i = 0; i < inode_read_list_end; i++) {

      if (inode_read_list[i].inode == p_inode) {

        build_path(inode_read_list[i].pathlist_self, path);
        LOG(L_SKIPPED, "Skipping [%s] due to duplicate inode.\n", path);

        int before = inode_read_list[i].pathlist_head->list_size;
        int after = mark_path_entry_invalid(inode_read_list[i].pathlist_head,
                                            inode_read_list[i].pathlist_self);
        s_files_hl_skip += (before - after);
      }
      p_inode = inode_read_list[i].inode;
    }

    free_inode_read_list();
  }

  // If we ran into a substantial number of files where the physical block(s)
  // were reported as zero, give up on using fiemap ordering.

  if (using_fiemap) {
    int zeropct = (100 * stats_fiemap_zero_blocks) / stats_fiemap_total_blocks;
    if (zeropct > 5 && s_total_files_seen > 100) {
      using_fiemap = 0;
      LOG(L_PROGRESS, "Turning off using_fiemap, %d%% zero blocks\n", zeropct);
    }
  }

  if (using_fiemap) {
    qsort(read_list, read_list_end,
          sizeof(struct read_list_entry), rl_compare_b);
  } else {
    qsort(read_list, read_list_end,
          sizeof(struct read_list_entry), rl_compare_i);
  }

  LOG_MORE_TRACE {
    printf("read_list after final sort\n");
    dump_read_list(0);
  }
}
