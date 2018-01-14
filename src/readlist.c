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

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "dirtree.h"
#include "main.h"
#include "paths.h"
#include "readlist.h"

struct read_list_entry * read_list = NULL;
long read_list_end;
static long read_list_size;


/** ***************************************************************************
 * Sort function used by sort_read_list().
 *
 */
static int rl_compare(const void * a, const void * b)
{
  struct read_list_entry * f = (struct read_list_entry *)a;
  struct read_list_entry * s = (struct read_list_entry *)b;

  if (f->device == s->device) {
    return f->inode - s->inode;
  } else {
    return f->device - s->device;
  }
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
void add_to_read_list(dev_t device, ino_t inode, struct path_list_head * head,
                      struct path_list_entry * entry)
{
  read_list[read_list_end].device = device;
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
void sort_read_list()
{
  qsort(read_list, read_list_end, sizeof(struct read_list_entry), rl_compare);

  // If hardlink_is_unique, we're always in hdd mode (forced in main).
  // Now that the read_list is ordered, remove any paths which are duplicate
  // inodes given that we don't care about them if hardlink_is_unique.

  if (hardlink_is_unique) {
    long i;
    ino_t prev = 0;
    for (i = 0; i < read_list_end; i++) {
      if (read_list[i].inode == prev) {
        char path[DUPD_PATH_MAX];
        build_path(read_list[i].pathlist_self, path);
        LOG(L_SKIPPED, "Skipping [%s] due to duplicate inode.", path);
        read_list[i].pathlist_head->list_size--;
        read_list[i].pathlist_head = NULL;
        {
          char * fname = pb_get_filename(read_list[i].pathlist_self);
          fname[0] =0; // TODO
        }
        read_list[i].pathlist_self = NULL;
      }
      prev = read_list[i].inode;
    }
  }
}
