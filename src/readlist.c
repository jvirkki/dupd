/*
  Copyright 2016-2021 Jyri J. Virkki <jyri@virkki.com>

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
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "dirtree.h"
#include "main.h"
#include "paths.h"
#include "readlist.h"
#include "sizelist.h"
#include "stats.h"
#include "utils.h"

#define INITIAL_READ_LIST_SIZE 100000

#define SMALL_GROUP_SMALL_FILES_LIMIT 512
#define SMALL_GROUP_LARGE_FILES_LIMIT 8

static uint64_t read_block_counter = 0;

struct read_list_entry * read_list = NULL;
uint64_t read_list_end;

struct read_list_entry * inode_read_list = NULL;
static uint64_t inode_read_list_end;
static uint64_t inode_read_list_size;


/** ***************************************************************************
 * Dump read list.
 *
 */
                                                             // LCOV_EXCL_START
static void dump_read_list(int with_path_list)
{
  char path[DUPD_PATH_MAX];

  printf("--- dumping read_list ---\n");
  for (uint64_t i = 0; i < read_list_end; i++) {
    build_path(read_list[i].pathlist_self, path);
    printf("[%" PRIu64 "] inode: %8ld  %" PRIu64 " (%d of size %" PRIu64
           ") %s\n",
           i, (long)read_list[i].inode, read_list[i].block,
           read_list[i].pathlist_head->list_size,
           read_list[i].pathlist_head->sizelist->size,
           path);
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
  if (hardlink_is_unique) {
    inode_read_list_size = INITIAL_READ_LIST_SIZE;
    if (x_small_buffers) { inode_read_list_size = 8; }
    inode_read_list_end = 0;
    inode_read_list =
      (struct read_list_entry *)calloc(inode_read_list_size,
                                       sizeof(struct read_list_entry));
    inc_stats_readlist(sizeof(struct read_list_entry) * inode_read_list_size);
  }
}


/** ***************************************************************************
 * Public function, see readlist.h
 *
 */
void free_read_list()
{
  if (read_list != NULL) {
    dec_stats_readlist(sizeof(struct read_list_entry) * read_block_counter);
    free(read_list);
    read_list = NULL;
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
    dec_stats_readlist(sizeof(struct read_list_entry) * inode_read_list_size);
    free(inode_read_list);
    inode_read_list = NULL;
    inode_read_list_size = 0;
    inode_read_list_end = 0;
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
      inc_stats_readlist(sizeof(struct read_list_entry) * inode_read_list_size);
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

  read_block_counter += entry->blocks->count;

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
 * Add all blocks of all files (in FS_NEED_DATA state) in a given path list
 * to the tmp_read_list provided.
 *
 */
static uint64_t
add_all_blocks_from_group(struct path_list_head * plhead,
                          struct read_list_entry * tmp_read_list,
                          uint64_t * tmp_index)
{
  uint64_t n = 0;
  struct path_list_entry * entry = pb_get_first_entry(plhead);

  while (entry != NULL) {
    if (entry->state == FS_NEED_DATA) {
      for (uint8_t i = 0; i < entry->blocks->count; i++) {
        tmp_read_list[*tmp_index].pathlist_head = plhead;
        tmp_read_list[*tmp_index].pathlist_self = entry;
        tmp_read_list[*tmp_index].block = entry->blocks->entry[i].block;
        tmp_read_list[*tmp_index].inode = 0;
        tmp_read_list[*tmp_index].done = 0;
        (*tmp_index)++;
        n++;
      }
    }
    entry = entry->next;
  }

  return n;
}


/** ***************************************************************************
 * Sort the blocks in tmp_read_list and append to the new_read_list.
 *
 */
static void sort_and_transfer(struct read_list_entry * tmp_read_list,
                              uint64_t * tmp_index,
                              struct read_list_entry * new_read_list,
                              uint64_t * new_index)
{
  qsort(tmp_read_list, *tmp_index,
        sizeof(struct read_list_entry), rl_compare_b);

  memcpy(&(new_read_list[*new_index]), tmp_read_list,
         sizeof(struct read_list_entry) * (*tmp_index));

  (*new_index) += *tmp_index;
}


/** ***************************************************************************
 * Public function, see readlist.h
 *
 */
void sort_read_list()
{
  if (hardlink_is_unique) {
    qsort(inode_read_list, inode_read_list_end,
          sizeof(struct read_list_entry), rl_compare_i);

    // Now that the inode_read_list is ordered, remove any paths which
    // are duplicate inodes given that we don't care about them.

    char path[DUPD_PATH_MAX];
    uint64_t i;
    ino_t p_inode = 0;

    for (i = 0; i < inode_read_list_end; i++) {

      if (inode_read_list[i].inode == p_inode) {

        build_path(inode_read_list[i].pathlist_self, path);
        LOG(L_SKIPPED, "Skipping [%s] due to duplicate inode.\n", path);

        int before = inode_read_list[i].pathlist_head->list_size;
        int after =
          mark_path_entry_ignore_hardlink(inode_read_list[i].pathlist_head,
                                          inode_read_list[i].pathlist_self);
        s_files_hl_skip += (before - after);
      }
      p_inode = inode_read_list[i].inode;
    }

    free_inode_read_list();
  }


  // For the normal case we don't have a block list yet so let's build one.
  // We know there are 'read_block_counter' blocks to sort (might be inodes
  // or extent blocks). The list will be built in groups into 'the_read_list'.

  struct read_list_entry * the_read_list = (struct read_list_entry *)
    malloc(sizeof(struct read_list_entry) * read_block_counter);
  inc_stats_readlist(sizeof(struct read_list_entry) * read_block_counter);
  uint64_t read_list_index = 0;

  struct read_list_entry * tmp_read_list = (struct read_list_entry *)
    malloc(sizeof(struct read_list_entry) * read_block_counter);
  inc_stats_readlist(sizeof(struct read_list_entry) * read_block_counter);
  uint64_t tmp_index = 0;

  uint64_t block_counter = 0;
  uint64_t set_counter = 0;
  struct size_list * szl = NULL;

  // So, need to select an order for reading the blocks. For best HDD
  // performance we can read the blocks in strict disk order (on Linux
  // we know this via the extent blocks, elsewhere the inode order is a
  // good approximation). However.. this can lead to extremely high memory
  // usage if the blocks are scattered just right(wrong) so that we end
  // up buffering lots of partial files which we can't hash quite yet.
  // (The memory usage is ultimately capped and the relief valve is
  // size_list_flusher(), but having to rely on that is very slow.)
  //
  // To reduce (not eliminate) this we can do several passes by grouping
  // files with similar size characteristics. The selections below may
  // need some tuning, but ultimately there isn't any one optimal order
  // as it will depend on the file set being scanned.

  // 1: Handle all groups of files smaller than a single read block.
  // All these files will be read in a single read() so for each file there
  // can't be any further pending reads.

  tmp_index = 0;
  block_counter = 0;
  set_counter = 0;
  szl = size_list_head;
  while (szl != NULL) {
    if (szl->size <= hash_one_block_size) {
      block_counter +=
        add_all_blocks_from_group(szl->path_list, tmp_read_list, &tmp_index);
      set_counter++;
    }
    szl = szl->next;
  }
  sort_and_transfer(tmp_read_list, &tmp_index,the_read_list, &read_list_index);
  if (set_counter > 0) {
    LOG(L_INFO, "read_list: (#1 small files): "
        "SETS %" PRIu64 ", BLOCKS %" PRIu64 "\n", set_counter, block_counter);
  }

  // 2: Handle groups of files larger than a single read block but still
  // smaller than the size limit we're willing to hash at once, but only
  // if the group is not too large. This cap is to prevent the edge case
  // where there are many thousands of relatively large files of the same
  // size, which would lead to the high memory consumption we're trying
  // to prevent.

  tmp_index = 0;
  block_counter = 0;
  set_counter = 0;
  szl = size_list_head;
  while (szl != NULL) {
    if (szl->path_list->list_size <= SMALL_GROUP_SMALL_FILES_LIMIT &&
        szl->size > hash_one_block_size && szl->size <= round1_max_bytes) {
      block_counter +=
        add_all_blocks_from_group(szl->path_list, tmp_read_list, &tmp_index);
      set_counter++;
    }
    szl = szl->next;
  }
  sort_and_transfer(tmp_read_list, &tmp_index,the_read_list, &read_list_index);
  if (set_counter > 0) {
    LOG(L_INFO, "read_list: (#2 medium files): "
        "SETS %" PRIu64 ", BLOCKS %" PRIu64 "\n", set_counter, block_counter);
  }

  // 3: Then, for the very large groups we excluded above, add them
  // individually for each given size group. The groups may still be very
  // large but by grouping them at least we'll be progressing faster within
  // each group, usually leading to less buffering.

  tmp_index = 0;
  szl = size_list_head;
  while (szl != NULL) {
    if (szl->path_list->list_size > SMALL_GROUP_SMALL_FILES_LIMIT &&
        szl->size > hash_one_block_size && szl->size <= round1_max_bytes) {
      tmp_index = 0;
      block_counter =
        add_all_blocks_from_group(szl->path_list, tmp_read_list, &tmp_index);
      sort_and_transfer(tmp_read_list, &tmp_index,
                        the_read_list, &read_list_index);
      LOG(L_INFO, "read_list: (#3 large set, size: %" PRIu64 "): "
          "SETS 1, BLOCKS %" PRIu64 "\n", szl->size, block_counter);
    }
    szl = szl->next;
  }

  // 4: Next, include blocks for all large files in reasonably small sets.

  tmp_index = 0;
  block_counter = 0;
  set_counter = 0;
  szl = size_list_head;
  while (szl != NULL) {
    if (szl->path_list->list_size <= SMALL_GROUP_LARGE_FILES_LIMIT &&
        szl->size > round1_max_bytes) {
      block_counter +=
        add_all_blocks_from_group(szl->path_list, tmp_read_list, &tmp_index);
      set_counter++;
    }
    szl = szl->next;
  }
  sort_and_transfer(tmp_read_list, &tmp_index,the_read_list, &read_list_index);
  if (set_counter > 0) {
    LOG(L_INFO, "read_list: (#4 large files): "
        "SETS %" PRIu64 ", BLOCKS %" PRIu64 "\n", set_counter, block_counter);
  }

  // 5: And finally, large sets of large files. Had to tackle it at some
  // point. These are added individually per set to try to limit memory usage.

  tmp_index = 0;
  szl = size_list_head;
  while (szl != NULL) {
    if (szl->path_list->list_size > SMALL_GROUP_LARGE_FILES_LIMIT &&
        szl->size > round1_max_bytes) {
      tmp_index = 0;
      block_counter =
        add_all_blocks_from_group(szl->path_list, tmp_read_list, &tmp_index);
      sort_and_transfer(tmp_read_list, &tmp_index,
                        the_read_list, &read_list_index);
      LOG(L_INFO, "read_list: (#5 large set, size: %" PRIu64 "): "
          "SETS 1, BLOCKS %" PRIu64 "\n", szl->size, block_counter);
    }
    szl = szl->next;
  }

  // Done! All blocks should be accounted for.

  free(tmp_read_list);
  tmp_read_list = NULL;
  tmp_index = 0;
  dec_stats_readlist(sizeof(struct read_list_entry) * read_block_counter);

  read_list = the_read_list;
  read_list_end = read_list_index;

  // If we ran into a substantial number of files where the physical block(s)
  // were reported as zero, give up on using fiemap ordering. Shouldn't
  // happen but if it does, revert to inodes.

  if (using_fiemap) {
    int zeropct = (100 * stats_fiemap_zero_blocks) / stats_fiemap_total_blocks;
    if (zeropct > 5 && s_total_files_seen > 100) {
      using_fiemap = 0;
      LOG(L_PROGRESS, "Turning off using_fiemap, %d%% zero blocks\n", zeropct);
    }
  }

  LOG_MORE_TRACE {
    printf("read_list after final sort\n");
    dump_read_list(0);
  }
}
