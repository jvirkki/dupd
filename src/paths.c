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
#include "sizelist.h"
#include "sizetree.h"
#include "stats.h"
#include "utils.h"

static long block_size;
static char * path_block;
static char * next_entry;
static char * path_block_end;


/** ***************************************************************************
 * Debug function. Dumps the path list for a given size starting from head.
 *
 */
static void dump_path_list(const char * line, long size, char * head)
{
  printf("----- dump_size_list for size %ld -----\n", size);
  printf("%s\n", line);

  printf("  head: %p\n", head);

  char * last_elem = pl_get_last_entry(head);
  printf("  last_elem: %p\n", last_elem);

  uint32_t list_len = pl_get_path_count(head);
  printf("  list_len: %d\n", (int)list_len);

  char * first_elem = pl_get_first_entry(head);
  printf("  first_elem: %p\n", first_elem);

  uint32_t counted = 1;

  char * here = first_elem;
  while (here != NULL) {
    if (counted < 2 || verbosity >= 7) {
      printf("   [%s]\n", here + sizeof(char *));
      printf("   next: %p\n", *(char **)here);
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
 * Public function, see paths.h
 *
 */
void init_path_block()
{
  block_size = file_count * avg_path_len;
  path_block = (char *)malloc(block_size);
  if (path_block == NULL) {                                  // LCOV_EXCL_START
    printf("Unable to allocate path block!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if (verbosity >= 4) {
    printf("Allocated %ld bytes for the path block.\n", block_size);
  }

  next_entry = path_block;
  path_block_end = path_block + block_size;
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void free_path_block()
{
  if (path_block != NULL) {
    free(path_block);
  }
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
char * insert_first_path(char * path)
{
  char * head = next_entry;
  char * new_entry = pl_get_first_entry(head);

  // See paths.h for documentation on structure

  // Initialize ListSize (to 1)
  pl_init_path_count(head);

  // PTR2LAST - Set to point to self since we're the first and last elem now
  pl_entry_set_next(head, new_entry);

  // And update PTR2NEXT of the new (now last) entry we just added to NULL
  pl_entry_set_next(new_entry, NULL);

  // Copy path string to new entry
  strcpy(pl_entry_get_path(new_entry), path);

  // Update top of free space to point beyond the space we just used up
  next_entry = new_entry + sizeof(char *) + strlen(path) + 1;

  if (verbosity > 6) {
    dump_path_list("AFTER insert_first_path", -1, head);
  }

  if (next_entry > path_block_end) {                         // LCOV_EXCL_START
    printf("error: path block too small!\n");
    printf("See --file-count and --avg-size options.\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  return head;
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void insert_end_path(char * path, long size, char * head)
{
  char * prior = NULL;
  char * new_entry = next_entry;

  if (pl_get_path_count(head) == 1) {

    // If there is only one entry in this path list, it means we are
    // adding the second element to this path list. Which in turn means
    // we have just identified a size which is a candidate for duplicate
    // processing later, so add it to the size list now.

    add_to_size_list(size, head);
    prior = pl_get_first_entry(head);

  } else {
    // Just jump to the end of the path list
    prior = pl_get_last_entry(head);
  }

  // Update PTR2LAST to point to the new last entry
  pl_entry_set_next(head, new_entry);

  // Update prior (previous last) PTR2NEXT to also point to the new last entry
  pl_entry_set_next(prior, new_entry);

  // And update PTR2NEXT of the new (now last) entry we just added to NULL
  pl_entry_set_next(new_entry, NULL);

  // Copy path string to new entry
  strcpy(pl_entry_get_path(new_entry), path);

  // Increase ListSize of this path list
  uint32_t path_count = pl_increase_path_count(head);

  // Update top of free space to point beyond the space we just used up
  next_entry = new_entry + sizeof(char *) + strlen(path) + 1;

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
  long used = (long)(next_entry - path_block);
  int pct = (int)((100 * used) / block_size);
  printf("Total path block size: %ld\n", block_size);
  printf("Bytes used in this run: %ld (%d%%)\n", used, pct);
}
