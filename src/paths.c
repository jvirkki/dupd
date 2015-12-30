/*
  Copyright 2012-2015 Jyri J. Virkki <jyri@virkki.com>

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

#include <inttypes.h>
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

  char * last_elem = *(char **)head;
  printf("  last_elem: %p\n", last_elem);

  char * list_len_ptr = head + 1 * sizeof(char *);
  uint32_t list_len = (uint32_t)*(uint32_t *)list_len_ptr;
  printf("  list_len: %d\n", (int)list_len);

  char * first_elem = head + 2 * sizeof(char *);
  printf("  first_elem: %p\n", first_elem);

  char * here = first_elem;
  while (here != NULL) {
    printf("   [%s]\n", here + sizeof(char *));
    printf("   next: %p\n", *(char **)here);
    here = *(char **)here;
  }
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
  char * rv = next_entry;

  // See paths.h for documentation on structure

  // PTR2LAST - Set to point to self since we're the first and last elem now
  *(char **)(rv + 0 * sizeof(char *)) = (char *)(rv + 2 * sizeof(char *));

  // ListSize - Now 1 since we're adding the first and only elem
  *(char **)(rv + 1 * sizeof(char *)) = (char *)1;

  // PTR2NEXT - NULL, no other elements yet
  *(char **)(rv + 2 * sizeof(char *)) = NULL;

  // path string...
  strcpy((char *)(rv + 3 * sizeof(char *)), path);

  // Update top of free space to point beyond the space we just used up
  next_entry = next_entry + (3 * sizeof(char *)) + 1 + strlen(path);

  if (verbosity > 6) {
    dump_path_list("AFTER insert_first_path", -1, rv);
  }

  if (next_entry > path_block_end) {                         // LCOV_EXCL_START
    printf("error: path block too small!\n");
    printf("See --file-count and --avg-size options.\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  return rv;
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void insert_end_path(char * path, long size, char * head)
{
  char * last_elem = *(char **)head;
  char * list_len = head + 1 * sizeof(char *);
  char * first_elem = head + 2 * sizeof(char *);
  char * second_elem = *(char **)(head + 2 * sizeof(char *));
  char * prior = NULL;

  if (second_elem == NULL) {
    // This means we are now adding the second element to this path list.
    // Which means we have a size which is a candidate for duplicate
    // processing later, so add it to the size list.
    add_to_size_list(size, head);
    prior = first_elem;

  } else {
    // Jump to the end of the path list
    prior = last_elem;
  }

  // PTR2LAST - Set to point to next_entry, where the new entry will be
  *(char **)head = next_entry;

  // PTR2NEXT - Set previous last element to point to new element as well
  *(char **)prior = next_entry;

  // path string...
  strcpy(next_entry + sizeof(char *), path);

  // PTR2NEXT - Next of new elem
  *(char **)next_entry = NULL;

  // Increase ListSize of this path list
  uint32_t path_count = (uint32_t)*(uint32_t *)list_len;
  path_count++;
  *(uint32_t *)list_len = path_count;

  // Update top of free space to point beyond the space we just used up
  next_entry = next_entry + sizeof(char *) + strlen(path) + 1;

  if (verbosity >= 6) {
    dump_path_list("AFTER insert_end_path", size, head);
  }

  if (path_count > stats_max_pathlist) {
    stats_max_pathlist = path_count;
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
