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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "dbops.h"
#include "dirtree.h"
#include "dtrace.h"
#include "hash.h"
#include "main.h"
#include "paths.h"
#include "readlist.h"
#include "sizelist.h"
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
void * fiemap = NULL;


/** ***************************************************************************
 * Debug function. Dumps the path list for a given size starting from head.
 *
 */
                                                             // LCOV_EXCL_START
void dump_path_list(const char * line, uint64_t size,
                    struct path_list_head * head, int dump_all)
{
  printf("----- dump path block list for size %ld -----\n", (long)size);
  printf("%s\n", line);

  printf("  head: %p\n", head);
  printf("  last_elem: %p\n", head->last_entry);
  printf("  list_size: %d\n", head->list_size);
  printf("  wanted_bufsize: %" PRIu32 "\n", head->wanted_bufsize);
  printf("  buffer_ready: %d\n", head->buffer_ready);
  printf("  state: %s\n", pls_state(head->state));
  printf("  hash_passes: %d\n", head->hash_passes);
  printf("  have_cached_hashes: %d\n", head->have_cached_hashes);
  printf("  sizelist back ptr: %p\n", head->sizelist);

  if (head->sizelist != NULL) {
    printf("   forward ptr back to me: %p\n", head->sizelist->path_list);
    if (head->sizelist->path_list != head) {
      printf("error: mismatch!\n");
      exit(1);
    }
  }

  struct path_list_entry * entry = pb_get_first_entry(head);
  printf("  first_elem: %p\n", entry);

  uint32_t counted = 1;
  uint32_t valid = 0;
  char buffer[DUPD_PATH_MAX];
  char * filename;

  while (entry != NULL) {
    if (counted < 2 || log_level >= L_TRACE || dump_all) {
      printf(" --entry %d\n", counted);
      printf("   file state: %s\n", file_state(entry->state));
      printf("   filename_size: %d\n", entry->filename_size);
      printf("   dir: %p\n", entry->dir);
      printf("   fd: %d\n", entry->fd);
      printf("   next: %p\n", entry->next);
      printf("   buffer: %p\n", entry->buffer);
      printf("   bufsize: %" PRIu32 "\n", entry->bufsize);
      printf("   data_in_buffer: %" PRIu32 "\n", entry->data_in_buffer);
      printf("   file_pos: %" PRIu64 "\n", entry->file_pos);
      printf("   next_read_byte: %" PRIu64 "\n", entry->next_read_byte);
      printf("   next_buffer_pos: %" PRIu32 "\n", entry->next_buffer_pos);
      printf("   next_read_block: %d\n", entry->next_read_block);
      printf("   blocks: %p\n", entry->blocks);
      printf("   hash_ctx: %p\n", entry->hash_ctx);
      dump_block_list("      ", entry->blocks);

      filename = pb_get_filename(entry);
      bzero(buffer, DUPD_PATH_MAX);
      memcpy(buffer, filename, entry->filename_size);
      buffer[entry->filename_size] = 0;
      printf("   filename (direct read): [%s]\n", buffer);
      bzero(buffer, DUPD_PATH_MAX);
      build_path(entry, buffer);
      printf("   built path: [%s]\n", buffer);
      if (entry->state != FS_UNIQUE &&
          entry->state != FS_IGNORE &&
          entry->state != FS_IGNORE_HL) {
        valid++;
      }
    }
    counted++;
    entry = entry->next;
  }

  counted--;
  printf("counted entries: %d\n", counted);
  printf("valid entries: %d\n", valid);
  if (valid != head->list_size) {
    printf("list_len (%d)!=valid entries (%d)\n", head->list_size, valid);
    exit(1);
  }

  printf("-----\n\n\n");
}
                                                             // LCOV_EXCL_STOP


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

  if (next->ptr == NULL) {                                   // LCOV_EXCL_START
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
 * Clear remaining entry in a path list by marking it FS_UNIQUE.
 *
 */
static int clear_remaining_entry(struct path_list_head * head)
{
  head->state = PLS_DONE;
  head->list_size = 0;
  d_mutex_lock(&stats_lock, "mark invalid stats");
  stats_sets_dup_not[ROUND1]++;
  d_mutex_unlock(&stats_lock);
  LOG(L_TRACE, "Reduced list size to %d, state now DONE\n", head->list_size);

  struct path_list_entry * e = pb_get_first_entry(head);
  int good = 0;

  while (e != NULL) {
    switch (e->state) {

    case FS_NEED_DATA:
    case FS_BUFFER_READY:
    case FS_CACHE_DONE:
      free_path_entry(e);
      good++;
      // XXX dtrace
      e->state = FS_UNIQUE;
      break;

    case FS_UNIQUE:
    case FS_IGNORE:
    case FS_IGNORE_HL:
      break;

    default:
                                                             // LCOV_EXCL_START
      printf("error: invalid state %s seen in clear_remaining_entry\n",
             file_state(e->state));
      dump_path_list("bad state", head->sizelist->size, head, 1);
      exit(1);
    }                                                        // LCOV_EXCL_STOP

    e = e->next;
  }
  return good;
}


/** ***************************************************************************
 * Shared internal implementation used by mark_path_entry_ignore() and
 * mark_path_entry_ignore_hl().
 *
 */
static int mark_path_entry_ignore_int(struct path_list_head * head,
                                      struct path_list_entry * entry,
                                      int ignore_state)
{
  entry->state = ignore_state;
  head->list_size--;
  free_path_entry(entry);
  LOG(L_TRACE, "ignore: reduced list size to %d\n", head->list_size);

  // If list is down to one entry, it's also unique and no more work remains
  if (head->list_size == 1) {
    int found = clear_remaining_entry(head);
    if (found != 1) {
      printf("error: clear_remaining_entry in mark_path_entry_ignore expected "
             " one remaining entry but saw %d\n", found);
      dump_path_list("", head->sizelist->size, head, 1);
      exit(1);
    }
  }

  // After shrinking list_size, we might now have all remaining entries ready
  if (head->list_size > 1 && head->list_size == head->buffer_ready) {
    head->state = PLS_ALL_BUFFERS_READY;
    LOG(L_TRACE, "After shrinking list_size to %d, state now %s\n",
        head->list_size, pls_state(head->state));
  }

  if (debug_size == head->sizelist->size) {
    dump_path_list("OUT mark_path_entry_ignore", head->sizelist->size, head,1);
  }

  if (head->list_size == UINT16_MAX) {
    printf("error: mark_path_entry_ignore_int list_size=-1\n");
    dump_path_list("", head->sizelist->size, head, 1);
    exit(1);
  }

  return head->list_size;
}


/** ***************************************************************************
 * Set the state on new path_list_entry.
 *
 */
static void mark_path_entry_new(struct path_list_entry * entry,
                                char * filename)
{
  (void)filename;
  entry->state = FS_NEED_DATA;
  DTRACE_PROBE3(dupd, set_state_new, filename, 0, FS_NEED_DATA);
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

  if (using_fiemap) {
    fiemap = fiemap_alloc();
  }
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

  if (fiemap != NULL) {
    free(fiemap);
  }
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void free_path_entry(struct path_list_entry * entry)
{
  if (entry->buffer != NULL) {
    free(entry->buffer);
    entry->buffer = NULL;
    dec_stats_read_buffers_allocated(entry->bufsize);
    entry->bufsize = 0;
    entry->data_in_buffer = 0;
  }

  if (entry->hash_ctx != NULL) {
    hash_fn_buf_free(entry->hash_ctx);
    entry->hash_ctx = NULL;
  }

  if (entry->blocks != NULL) {
    free(entry->blocks);
    entry->blocks = NULL;
  }

  if (entry->fd != 0) {
    close(entry->fd);
    entry->fd = 0;
    update_open_files(-1);
  }
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
struct path_list_head * insert_first_path(char * filename,
                                          struct direntry * dir_entry,
                                          uint64_t size)
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

  // Haven't read anything yet
  head->buffer_ready = 0;
  head->hash_passes = 0;
  head->have_cached_hashes = 1;
  head->wanted_bufsize = 0;

  // New path list
  head->state = PLS_NEED_DATA;

  // Initialize the first entry
  s_files_processed++;
  first_entry->filename_size = (uint8_t)filename_len;
  first_entry->fd = 0;
  first_entry->dir = dir_entry;
  first_entry->blocks = NULL;
  first_entry->hash_ctx = NULL;
  first_entry->file_pos = 0;
  first_entry->next_read_byte = 0;
  first_entry->next_buffer_pos = 0;
  first_entry->next_read_block = 0;
  first_entry->next = NULL;
  first_entry->buffer = NULL;
  first_entry->bufsize = 0;
  first_entry->data_in_buffer = 0;
  memcpy(filebuf, filename, filename_len);
  mark_path_entry_new(first_entry, filename);

  LOG_EVEN_MORE_TRACE {
    dump_path_list("AFTER insert_first_path", size, head, 0);
  }

  if (debug_size == size) {
    dump_path_list("AFTER insert_first_path", size, head, 1);
  }

  stats_path_list_entries++;

  return head;
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void insert_end_path(char * filename, struct direntry * dir_entry,
                     ino_t inode, uint64_t size, struct path_list_head * head)
{
  char pathbuf[DUPD_PATH_MAX];
  struct block_list * block_list = NULL;

  int filename_len = strlen(filename);
  int space_needed = sizeof(struct path_list_entry) + filename_len;
  check_space(space_needed);
  space_used += space_needed;

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
  s_files_processed++;
  entry->filename_size = (uint8_t)filename_len;
  entry->fd = 0;
  entry->dir = dir_entry;
  entry->next = NULL;
  entry->buffer = NULL;
  entry->hash_ctx = NULL;
  entry->bufsize = 0;
  entry->data_in_buffer = 0;
  entry->blocks = NULL;
  entry->file_pos = 0;
  entry->next_read_byte = 0;
  entry->next_buffer_pos = 0;
  entry->next_read_block = 0;
  memcpy(filebuf, filename, filename_len);
  mark_path_entry_new(entry, filename);

  // If there are now two entries in this path list, it means we have
  // just identified a size which is a candidate for duplicate
  // processing later, so add it to the size list now.

  if (head->list_size == 2) {
    struct size_list * new_szl = add_to_size_list(size, head);
    head->sizelist = new_szl;

    if (size <= round1_max_bytes) {
      head->wanted_bufsize = size;
    } else {
      head->wanted_bufsize = hash_one_block_size;
    }

    STRUCT_STAT info;

    // Add the first entry to the read list. It wasn't added earlier
    // because we didn't know it needed to be there but now we do.
    // We'll need to re-stat() it to get info. This should be fast
    // because it should be in the cache already. (Alternatively,
    // could keep this info in the path list head.)

    build_path(prior, pathbuf);
    if (get_file_info(pathbuf, &info)) {                   // LCOV_EXCL_START
      printf("error: unable to stat %s\n", pathbuf);
      exit(1);
    }                                                      // LCOV_EXCL_STOP

    block_list = get_block_info_from_path(pathbuf, info.st_ino, size,fiemap);
    prior->blocks = block_list;
    add_to_read_list(head, prior, info.st_ino);

    if (use_hash_cache && size > cache_min_size) {
      if (cache_db_check_entry(pathbuf) != CACHE_HASH_FOUND) {
        head->have_cached_hashes = 0;
      }
    } else {
      head->have_cached_hashes = 0;
    }
  }

  build_path_from_string(filename, dir_entry, pathbuf);
  block_list = get_block_info_from_path(pathbuf, inode, size, fiemap);
  entry->blocks = block_list;
  add_to_read_list(head, entry, inode);

  if (use_hash_cache && size > cache_min_size) {
    if (cache_db_check_entry(pathbuf) != CACHE_HASH_FOUND) {
      head->have_cached_hashes = 0;
    }
  }

  LOG_EVEN_MORE_TRACE {
    dump_path_list("AFTER insert_end_path", size, head, 0);
  }

  if (debug_size == size) {
    dump_path_list("AFTER insert_end_path", size, head, 1);
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


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
const char * pls_state(int state)
{
  switch(state) {
  case PLS_NEED_DATA:                return "PLS_NEED_DATA";
  case PLS_ALL_BUFFERS_READY:        return "PLS_ALL_BUFFERS_READY";
  case PLS_DONE:                     return "PLS_DONE";
  default:
    printf("\nerror: unknown pls_state %d\n", state);
    exit(1);
  }
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
const char * file_state(int state)
{
  switch(state) {
  case FS_NEED_DATA:                  return "FS_NEED_DATA";
  case FS_BUFFER_READY:               return "FS_BUFFER_READY";
  case FS_DONE:                       return "FS_DONE";
  case FS_CACHE_DONE:                 return "FS_CACHE_DONE";
  case FS_UNIQUE:                     return "FS_UNIQUE";
  case FS_IGNORE:                     return "FS_IGNORE";
  case FS_IGNORE_HL:                  return "FS_IGNORE_HL";
  default:
    printf("\nerror: unknown file_state %d\n", state);
    exit(1);
  }
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void mark_path_entry_unique(struct path_list_head * head,
                            struct path_list_entry * entry)
{
  if (debug_size == head->sizelist->size) {
    dump_path_list("IN mark_path_entry_unique", head->sizelist->size, head, 1);
  }

  if (entry->state == FS_UNIQUE) {
    LOG(L_TRACE, "mark_path_entry_unique: entry is already FS_UNIQUE, skip\n");
    return;
  }

  // XXX check different preconditions for each acceptable input state?

  // States which can transition to FS_UNIQUE
  if (entry->state != FS_NEED_DATA && entry->state != FS_CACHE_DONE) {
                                                             // LCOV_EXCL_START
    printf("error: set entry state FS_UNIQUE but current state is %s\n",
           file_state(entry->state));
    dump_path_list("bad state", head->sizelist->size, head, 1);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if (head->list_size == 0) {
                                                             // LCOV_EXCL_START
    printf("error: set entry state FS_UNIQUE but list size is zero!!\n");
    dump_path_list("bad state", head->sizelist->size, head, 1);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  entry->state = FS_UNIQUE;
  head->list_size--;
  free_path_entry(entry);
  LOG(L_TRACE, "unique: reduced list size to %d\n", head->list_size);

  // After shrinking list_size, we might now have all remaining entries ready
  if (head->list_size == head->buffer_ready) {
    printf("XXXX setUNIQUE need to deal with all buffers full\n");
    exit(1);
  }

  if (head->list_size == 1) {
    int found = clear_remaining_entry(head);
    if (found != 1) {
      printf("error: clear_remaining_entry in mark_path_entry_unique expected "
             " one remaining entry but saw %d\n", found);
      dump_path_list("", head->sizelist->size, head, 1);
    }
  }

  if (debug_size == head->sizelist->size) {
    dump_path_list("OUT mark_path_entry_unique", head->sizelist->size, head,1);
  }
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
int mark_path_entry_ignore(struct path_list_head * head,
                           struct path_list_entry * entry)
{
  if (debug_size == head->sizelist->size) {
    dump_path_list("IN mark_path_entry_ignore", head->sizelist->size, head, 1);
  }

  if (entry->state != FS_NEED_DATA) {
                                                             // LCOV_EXCL_START
    printf("error: set entry state FS_IGNORE but current state is %s\n",
           file_state(entry->state));
    dump_path_list("bad state", head->sizelist->size, head, 1);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if (head->list_size == 0) {
                                                             // LCOV_EXCL_START
    printf("error: set entry state FS_IGNORE but list size is zero!!\n");
    dump_path_list("bad state", head->sizelist->size, head, 1);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  return mark_path_entry_ignore_int(head, entry, FS_IGNORE);
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
int mark_path_entry_ignore_hardlink(struct path_list_head * head,
                                    struct path_list_entry * entry)
{
  if (debug_size == head->sizelist->size) {
    dump_path_list("IN mark_path_entry_ignore", head->sizelist->size, head, 1);
  }

  // For FS_IGNORE_HL it is possible that this list is already empty when
  // hardlinks are dropped in sort_read_list(). In those cases, we're done.
  if (head->list_size == 0) {
    return 0;
  }

  if (entry->state != FS_NEED_DATA && entry->state != FS_UNIQUE) {
                                                             // LCOV_EXCL_START
    printf("error: set entry state FS_IGNORE_HL but current state is %s\n",
           file_state(entry->state));
    dump_path_list("bad state", head->sizelist->size, head, 1);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  return mark_path_entry_ignore_int(head, entry, FS_IGNORE_HL);
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void mark_path_entry_ready(struct path_list_head * head,
                           struct path_list_entry * entry)
{

  if (head->state != PLS_NEED_DATA) {
    printf("error: mark_path_entry_ready: head->state != PLS_NEED_DATA\n");
    dump_path_list("", head->sizelist->size, head, 1);
    exit(1);
  }

  if (entry->state != FS_NEED_DATA) {
    printf("error: mark_path_entry_ready: entry->state != FS_NEED_DATA\n");
    dump_path_list("", head->sizelist->size, head, 1);
    exit(1);
  }

  entry->state = FS_BUFFER_READY;
  head->buffer_ready++;

  if (head->buffer_ready == head->list_size) {
    head->state = PLS_ALL_BUFFERS_READY;

    LOG_INFO {
      struct path_list_entry * pe = pb_get_first_entry(head);
      uint32_t ready = 0;
      while (pe != NULL) {
        if (pe->state == FS_BUFFER_READY) { ready++; }
        pe = pe->next;
      }
      if (ready != head->list_size) {
        printf("error: ready=%d but list_size=%d\n", ready, head->list_size);
        dump_path_list("mark_path_entry_ready", head->sizelist->size, head, 1);
        exit(1);
      }
    }
  }
}
