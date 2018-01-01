/*
  Copyright 2017 Jyri J. Virkki <jyri@virkki.com>

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

  uint16_t len = (uint16_t)strlen(name);
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
 * Public function, see dirtree.h
 *
 */
void build_path(char * filename, struct direntry * entry, char * buffer)
{
  int name_len = strlen(filename);
  uint16_t pos = entry->total_size;

  // First copy the filename to the end of the path buffer
  buffer[pos] = '/';
  memcpy(buffer + pos + 1, filename, name_len);
  buffer[pos + name_len + 1] = 0;

  // Then walk up the tree filling parent directory name until done
  struct direntry * part = entry;
  while (part != NULL) {
    pos -= part->name_size;
    memcpy(buffer + pos, part->name, part->name_size);
    pos--;
    part = part->parent;
    if (part != NULL) { buffer[pos] = '/'; }
  }
}


/** ***************************************************************************
 * Public function, see dirtree.h
 *
 */
void print_direntry(struct direntry * entry)
{
  char line[DUPD_FILENAME_MAX];

  while (entry != NULL) {
    memcpy(line, entry->name, entry->name_size);
    line[entry->name_size] = 0;
    printf("direntry: name [%s] (len=%d) total_size=%d\n", line,
           entry->name_size, entry->total_size);
    entry = entry->parent;
  }
}
