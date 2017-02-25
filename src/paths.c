/*
  Copyright 2012-2016 Jyri J. Virkki <jyri@virkki.com>

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
#include <strings.h>
#include <unistd.h>

#include "hashlist.h"
#include "main.h"
#include "paths.h"
#include "readlist.h"
#include "sizelist.h"
#include "sizetree.h"
#include "stats.h"
#include "utils.h"

struct path_block_list {
  char * ptr;
  struct path_block_list * next;
};

static struct path_block_list * first_path_block = NULL;
static struct path_block_list * last_path_block = NULL;
static char * next_entry;
static char * path_block_end;
static long space_used;
static long space_allocated;


/** ***************************************************************************
 * Debug function. Dumps the path list for a given size starting from head.
 *
 */
void dump_path_list(const char * line, off_t size, char * head)
{
  printf("----- dump_size_list for size %ld -----\n", (long)size);
  printf("%s\n", line);

  printf("  head: %p\n", head);

  char * last_elem = pl_get_last_entry(head);
  printf("  last_elem: %p\n", last_elem);

  struct size_list * szl = pl_get_szl_entry(head);
  printf("  sizelist back ptr: %p\n", szl);
  if (szl != NULL) {
    printf("   forward ptr back to me: %p\n", szl->path_list);
    if (szl->path_list != head) {                            // LCOV_EXCL_START
      printf("error: mismatch!\n");
      exit(1);
    }                                                        // LCOV_EXCL_STOP
  }

  uint32_t list_len = pl_get_path_count(head);
  printf("  list_len: %d\n", (int)list_len);

  char * first_elem = pl_get_first_entry(head);
  printf("  first_elem: %p\n", first_elem);

  uint32_t counted = 1;

  char * here = first_elem;
  while (here != NULL) {
    if (counted < 2 || verbosity >= 7) {
      printf("   buffer: %p\n", pl_entry_get_buffer(here));
      printf("   [%s]\n", pl_entry_get_path(here));
      printf("   next: %p\n", pl_entry_get_next(here));
    }
    counted++;
    here = pl_entry_get_next(here);
  }

  counted--;
  printf("counted entries: %d\n", counted);
  if (counted != list_len) {
                                                             // LCOV_EXCL_START
    printf("list_len (%d) != counted entries (%d)\n", list_len, counted);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  printf("-----\n\n\n");
}


/** ***************************************************************************
 * Allocate a path block.
 *
 */
static struct path_block_list * alloc_path_block(int bsize)
{
  struct path_block_list * next;

  next = (struct path_block_list *)malloc(sizeof(struct path_block_list));
  next->next = NULL;
  next->ptr = (char *)malloc(bsize);

  if (next->ptr == NULL) {                                  // LCOV_EXCL_START
    printf("Unable to allocate new path block!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if (verbosity >= 4) {
    printf("Allocated %d bytes for the next path block.\n", bsize);
  }

  return next;
}


/** ***************************************************************************
 * Add another path block.
 *
 */
static void add_path_block()
{
  int bsize = 8 * 1024 * 1024;
  if (x_small_buffers) { bsize = PATH_MAX; }

  struct path_block_list * next = alloc_path_block(bsize);
  next_entry = next->ptr;
  path_block_end = next->ptr + bsize;
  last_path_block->next = next;
  last_path_block = next;

  space_allocated += bsize;
}


/** ***************************************************************************
 * Check if current path block can accomodate 'needed' bytes. If not, add
 * another path block.
 *
 */
inline static void check_space(int needed)
{
  if (path_block_end - next_entry - 2 <= needed) {
    add_path_block();
  }
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void init_path_block()
{
  int bsize = 1024 * 1024;
  if (x_small_buffers) { bsize = PATH_MAX; }

  first_path_block = alloc_path_block(bsize);
  next_entry = first_path_block->ptr;
  path_block_end = first_path_block->ptr + bsize;
  last_path_block = first_path_block;

  space_used = 0;
  space_allocated = bsize;
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void free_path_block()
{
  struct path_block_list * b;
  struct path_block_list * p;

  p = first_path_block;
  while (p != NULL) {
    free(p->ptr);
    b = p;
    p = b->next;
    free(b);
  }
  first_path_block = NULL;
  last_path_block = NULL;
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
char * insert_first_path(char * path)
{
  int space_needed = (2 * sizeof(char *)) + strlen(path) + 2;
  check_space((3 * sizeof(char *)) + space_needed); // first entry overhead
  space_used += (3 * sizeof(char *)) + space_needed;

  char * head = next_entry;
  char * new_entry = pl_get_first_entry(head);

  // See paths.h for documentation on structure

  // Initialize ListSize (to 1)
  pl_init_path_count(head);

  // The associated sizelist does not exist yet
  pl_set_szl_entry(head, NULL);

  // PTR2LAST - Set to point to self since we're the first and last elem now
  pl_entry_set_next(head, new_entry);

  // And update PTR2NEXT of the new (now last) entry we just added to NULL
  pl_entry_set_next(new_entry, NULL);

  // Set the byte buffer to NULL, nothing read yet
  pl_entry_set_buffer(new_entry, NULL);

  // Copy path string to new entry
  strcpy(pl_entry_get_path(new_entry), path);

  // Update top of free space to point beyond the space we just used up
  next_entry = new_entry + space_needed;

  if (next_entry >= path_block_end) {                        // LCOV_EXCL_START
    printf("error: path block too small!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if (verbosity > 6) {
    dump_path_list("AFTER insert_first_path", -1, head);
  }

  return head;
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void insert_end_path(char * path,
                     dev_t device, ino_t inode, off_t size, char * head)
{
  int space_needed = (2 * sizeof(char *)) + strlen(path) + 1;
  check_space(space_needed);
  space_used += space_needed;

  char * prior = NULL;
  char * new_entry = next_entry;

  if (pl_get_path_count(head) == 1) {

    // If there is only one entry in this path list, it means we are
    // adding the second element to this path list. Which in turn means
    // we have just identified a size which is a candidate for duplicate
    // processing later, so add it to the size list now.

    struct size_list * new_szl = add_to_size_list(size, head);
    pl_set_szl_entry(head, new_szl);

    if (verbosity >= 3) {
      struct size_list * szl = pl_get_szl_entry(head);
      if (szl != new_szl) {                                  // LCOV_EXCL_START
        printf("error: set szl to %p, but got back %p\n", new_szl, szl);
        exit(1);
      }                                                      // LCOV_EXCL_STOP
    }

    prior = pl_get_first_entry(head);

    if (hdd_mode) {
      // Add the first entry to the read list. It wasn't added earlier
      // because we didn't know it needed to be there but now we do.
      // We'll need to re-stat() it to get info. This should be fast
      // because it should be in the cache already. (Alternatively,
      // could keep this info in the path list head.)
      char * first_path = pl_entry_get_path(prior);
      STRUCT_STAT info;
      if (get_file_info(first_path, &info)) {                // LCOV_EXCL_START
        printf("error: unable to stat %s\n", first_path);
        exit(1);
      }                                                      // LCOV_EXCL_STOP
      add_to_read_list(info.st_dev, info.st_ino, head, prior);
    }

  } else {
    // Just jump to the end of the path list
    prior = pl_get_last_entry(head);
  }

  if (hdd_mode) {
    // Then add the current path to the read list as well.
    add_to_read_list(device, inode, head, new_entry);
  }

  // Update PTR2LAST to point to the new last entry
  pl_entry_set_next(head, new_entry);

  // Update prior (previous last) PTR2NEXT to also point to the new last entry
  pl_entry_set_next(prior, new_entry);

  // And update PTR2NEXT of the new (now last) entry we just added to NULL
  pl_entry_set_next(new_entry, NULL);

  // Set the byte buffer to NULL, nothing read yet
  pl_entry_set_buffer(new_entry, NULL);

  // Copy path string to new entry
  strcpy(pl_entry_get_path(new_entry), path);

  // Increase ListSize of this path list
  uint32_t path_count = pl_increase_path_count(head);

  // Update top of free space to point beyond the space we just used up
  next_entry = new_entry + space_needed;

  if (next_entry >= path_block_end) {                        // LCOV_EXCL_START
    printf("error: path block too small!\n");
    exit(1);
  }                                                           // LCOV_EXCL_STOP

  if (verbosity >= 6) {
    dump_path_list("AFTER insert_end_path", size, head);
  }

  if (path_count > stats_max_pathlist) {
    stats_max_pathlist = path_count;
    stats_max_pathlist_size = size;
  }
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void report_path_block_usage()
{
  int pct = (int)((100 * space_used) / space_allocated);
  printf("Total path block size: %ld\n", space_allocated);
  printf("Bytes used in this run: %ld (%d%%)\n", space_used, pct);
}
