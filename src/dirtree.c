/*
  Copyright 2017-2018 Jyri J. Virkki <jyri@virkki.com>

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

#include "dirtree.h"
#include "main.h"
#include "paths.h"


/** ***************************************************************************
 * Public function, see dirtree.h
 *
 */
struct direntry * new_child_dir(char * name, struct direntry * parent)
{
  struct direntry * entry = (struct direntry *)malloc(sizeof(struct direntry));
  if (!entry) {
    printf("error: unable to allocate direntry [%s]\n", name);
    exit(1);
  }

  uint8_t len = (uint8_t)strlen(name);
  entry->name_size = len;

  entry->name = (char *)malloc(len);
  if (!entry->name) {
    printf("error: unable to allocate direntry name\n");
    exit(1);
  }

  memcpy(entry->name, name, len);

  entry->parent = parent;

  if (parent == NULL) {
    entry->total_size = len;
  } else {
    entry->total_size = len + 1 + parent->total_size;
  }

  return entry;
}


/** ***************************************************************************
 * Completes filling the buffer with all path components.
 *
 * Parameters:
 *    filename - Pointer to the filename (may or may not be NULL-terminated).
 *    name_len - Length of filename.
 *    pos      - Start writing at this position (but we go backwards).
 *    buffer - Buffer for writing output, was allocated by caller.
 *    dir    - Start building path from this directory.
 *
 * Return: none (fills buffer)
 *
 */
static void internal_build_path(char * filename, int name_len,
                                uint16_t pos, char * buffer,
                                struct direntry * dir)
{
  // First copy the filename to the end of the path buffer
  buffer[pos] = '/';
  memcpy(buffer + pos + 1, filename, name_len);
  buffer[pos + name_len + 1] = 0;

  // Then walk up the tree filling parent directory name until done
  while (dir != NULL) {
    pos -= dir->name_size;
    memcpy(buffer + pos, dir->name, dir->name_size);
    pos--;
    dir = dir->parent;
    if (dir != NULL) { buffer[pos] = '/'; }
  }
}


/** ***************************************************************************
 * Public function, see dirtree.h
 *
 */
void build_path_from_string(char * filename, struct direntry * entry,
                            char * buffer)
{
  int name_len = strlen(filename);
  uint16_t pos = entry->total_size;

  internal_build_path(filename, name_len, pos, buffer, entry);
}


/** ***************************************************************************
 * Public function, see dirtree.h
 *
 */

void build_path(struct path_list_entry * entry, char * buffer)
{
  int name_len = entry->filename_size;
  uint16_t pos = entry->dir->total_size;
  char * filename = pb_get_filename(entry);

  internal_build_path(filename, name_len, pos, buffer, entry->dir);
}
