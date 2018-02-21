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


// Path lists (head + entries) are stored in path blocks which are preallocated
// as needed. This list holds the blocks we've had to allocate.

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
void dump_path_list(const char * line, off_t size,
                    struct path_list_head * head)
{
  printf("----- dump path block list for size %ld -----\n", (long)size);
  printf("%s\n", line);

  printf("  head: %p\n", head);
  printf("  last_elem: %p\n", head->last_entry);
  printf("  list_size: %d\n", head->list_size);
  printf("  sizelist back ptr: %p\n", head->sizelist);

  if (head->sizelist != NULL) {
    printf("   forward ptr back to me: %p\n", head->sizelist->path_list);
    if (head->sizelist->path_list != head) {                 // LCOV_EXCL_START
      printf("error: mismatch!\n");
      exit(1);
    }                                                        // LCOV_EXCL_STOP
  }

  struct path_list_entry * entry = pb_get_first_entry(head);
  printf("  first_elem: %p\n", entry);

  uint32_t counted = 1;
  uint32_t valid = 0;
  char buffer[DUPD_PATH_MAX];
  char * filename;

  while (entry != NULL) {
    if (counted < 2 || log_level >= L_TRACE) {
      printf(" --entry %d\n", counted);
      printf("   file state: %d\n", entry->file_state);
      printf("   filename_size: %d\n", entry->filename_size);
      printf("   dir: %p\n", entry->dir);
      printf("   next: %p\n", entry->next);
      printf("   buffer: %p\n", entry->buffer);

      filename = pb_get_filename(entry);
      if (filename[0] != 0) { // TODO
        bzero(buffer, DUPD_PATH_MAX);
        memcpy(buffer, filename, entry->filename_size);
        buffer[entry->filename_size] = 0;
        printf("   filename (direct read): [%s]\n", buffer);
        bzero(buffer, DUPD_PATH_MAX);
        build_path(entry, buffer);
        printf("   built path: [%s]\n", buffer);
        valid++;
      } else {
        printf("   filename: REMOVED EARLIER\n");
      }
    }
    counted++;
    entry = entry->next;
  }

  counted--;
  printf("counted entries: %d\n", counted);
  printf("valid entries: %d\n", valid);
  if (valid != head->list_size) {
                                                             // LCOV_EXCL_START
    printf("list_len (%d)!=valid entries (%d)\n", head->list_size, valid);
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

  LOG(L_RESOURCES, "Allocated %d bytes for the next path block.\n", bsize);

  return next;
}


/** ***************************************************************************
 * Add another path block.
 *
 */
static void add_path_block()
{
  int bsize = 8 * 1024 * 1024;
  if (x_small_buffers) { bsize = DUPD_PATH_MAX; }

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
  if (x_small_buffers) { bsize = DUPD_PATH_MAX; }

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
struct path_list_head * insert_first_path(char * filename,
                                          struct direntry * dir_entry)
{
  int filename_len = strlen(filename);

  int space_needed = filename_len +
    sizeof(struct path_list_head) + sizeof(struct path_list_entry);

  check_space(space_needed);
  space_used += space_needed;

  // The new list head will live at the top of the available space (next_entry)
  struct path_list_head * head = (struct path_list_head *)next_entry;

  // The first entry will live immediately after the head
  struct path_list_entry * first_entry =
    (struct path_list_entry *)((char *)head + sizeof(struct path_list_head));

  // And the filename of first entry lives immediately after its entry
  char * filebuf = pb_get_filename(first_entry);

  // Move the free space pointer forward for the amount of space we took above
  next_entry += space_needed;

  // The associated sizelist does not exist yet
  head->sizelist = NULL;

  // Last entry is the first entry since there's only one now
  head->last_entry = first_entry;

  // Initialize list size to 1
  head->list_size = 1;

  // Initialize the first entry
  first_entry->file_state = FST_NEW;
  first_entry->filename_size = (uint8_t)filename_len;
  first_entry->dir = dir_entry;
  first_entry->next = NULL;
  first_entry->buffer = NULL;
  memcpy(filebuf, filename, filename_len);

  LOG_TRACE {
    dump_path_list("AFTER insert_first_path", -1, head);
  }

  stats_path_list_entries++;

  return head;
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void insert_end_path(char * filename, struct direntry * dir_entry,
                     dev_t device, ino_t inode, off_t size,
                     struct path_list_head * head)
{
  int filename_len = strlen(filename);
  int space_needed = sizeof(struct path_list_entry) + filename_len;
  check_space(space_needed);
  space_used += space_needed;

  LOG_MORE_TRACE {
    dump_path_list("BEFORE insert_end_path", size, head);
  }

  // The entry will live at the top of the available space (next_entry)
  struct path_list_entry * entry = (struct path_list_entry *)next_entry;

  // And the filename of first entry lives immediately after its entry
  char * filebuf = pb_get_filename(entry);

  // Move the free space pointer forward for the amount of space we took above
  next_entry += space_needed;

  // Last entry in this list is now this one and the list grew by one
  struct path_list_entry * prior = head->last_entry;
  head->last_entry = entry;
  prior->next = entry;
  head->list_size++;

  // Initialize this new entry
  entry->file_state = FST_NEW;
  entry->filename_size = (uint8_t)filename_len;
  entry->dir = dir_entry;
  entry->next = NULL;
  entry->buffer = NULL;
  memcpy(filebuf, filename, filename_len);

  // If there are now two entries in this path list, it means we have
  // just identified a size which is a candidate for duplicate
  // processing later, so add it to the size list now.

  if (head->list_size == 2) {

    struct size_list * new_szl = add_to_size_list(size, head);
    head->sizelist = new_szl;

    if (hdd_mode) {
      // Add the first entry to the read list. It wasn't added earlier
      // because we didn't know it needed to be there but now we do.
      // We'll need to re-stat() it to get info. This should be fast
      // because it should be in the cache already. (Alternatively,
      // could keep this info in the path list head.)

      char first_path[DUPD_PATH_MAX];
      build_path(prior, first_path);

      STRUCT_STAT info;
      if (get_file_info(first_path, &info)) {                // LCOV_EXCL_START
        printf("error: unable to stat %s\n", first_path);
        exit(1);
      }                                                      // LCOV_EXCL_STOP
      add_to_read_list(info.st_dev, info.st_ino, head, prior);
    }
  }

  if (hdd_mode) {
    // Then add the current path to the read list as well.
    add_to_read_list(device, inode, head, entry);
  }

  LOG_TRACE {
    dump_path_list("AFTER insert_end_path", size, head);
  }

  if (head->list_size > stats_max_pathlist) {
    stats_max_pathlist = head->list_size;
    stats_max_pathlist_size = size;
  }

  stats_path_list_entries++;
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
  printf("Total files in path list: %" PRIu32 "\n", stats_path_list_entries);
}
