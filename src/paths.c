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
#include "utils.h"

static long block_size;
static char * path_block;
static char * next_entry;
static char * path_block_end;


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void init_path_block()
{
  block_size = file_count * avg_path_len;
  path_block = (char *)malloc(block_size);
  if (path_block == NULL) {
    printf("Unable to allocate path block!\n");
    exit(1);
  }

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

  *(char **)next_entry = (char *)NULL;       // points to next path list entry
  *(char **)((next_entry + sizeof(char *))) = (char *)1; // count of entries
  strcpy(next_entry + 2 * sizeof(char *), path); // the path string

  next_entry = next_entry + (2 * sizeof(char *)) + 1 + strlen(path);

  if (next_entry > path_block_end) {
    printf("error: path block too small!\n");
    printf("See --file-count and --avg-size options.\n");
    exit(1);
  }
  return rv;
}


/** ***************************************************************************
 * Public function, see paths.h
 *
 */
void insert_end_path(char * path, long size, char * first)
{
  char * prior = first;
  char * next = NULL;

  next = *(char **)first;
  if (next == NULL) {
    // If first->next is null, we are adding the second element to
    // this path list. This means we have a size which is a candidate
    // for duplicate processing later.
    add_to_size_list(size, first);

  } else {
    // Walk to the end of the path list
    while ( (next = *(char **)prior) != NULL) {
      prior = next;
    }
  }

  char * new_entry = insert_first_path(path);
  *(char **)prior = new_entry;

  // Increase path length counter on first node
  uint32_t path_count = (uint32_t)*(uint32_t *)((first + sizeof(uint32_t *)));
  path_count++;
  *(uint32_t *)((first + sizeof(uint32_t *))) = path_count;

  if (path_count > MAX_DUPLICATES) {
    printf("warning: a size set has %" PRIu32
           " entries, more than MAX_DUPLICATES(%d)\n",
           path_count, MAX_DUPLICATES);
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
