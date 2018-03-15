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

#ifndef _DUPD_DIRTREE_H
#define _DUPD_DIRTREE_H

#include <stdint.h>

#include "paths.h"

struct direntry {
  struct direntry * parent;
  char * name;              // NOT null-terminated directory name
  uint8_t name_size;        // Length of the 'name' string
  uint16_t total_size;      // Total length of the path (including self 'name')
};


/** ***************************************************************************
 * Initialize dirtree.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void init_dirtree();


/** ***************************************************************************
 * Free any dirtree memory.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void free_dirtree();


/** ***************************************************************************
 * Creates a node for the directory name, as a child of directory parent.
 *
 * If parent is NULL, this is a top level (prefix) entry.
 *
 * Parameters:
 *    name   - Name of the directory.
 *    parent - Pointer to parent (may be NULL if this is top level prefix).
 *
 * Return: pointer to new directory entry
 *
 */
struct direntry * new_child_dir(char * name, struct direntry * parent);


/** ***************************************************************************
 * Build a path string given a filename and a direntry.
 *
 * Parameters:
 *    filename - Name of the file, null-terminated string.
 *    entry    - Directory entry of the dir where filename is located.
 *    buffer   - Output buffer, must have been allocated by caller.
 *
 * Return: none
 *
 */
void build_path_from_string(char * filename, struct direntry * entry,
                            char * buffer);


/** ***************************************************************************
 * Build a path string given a path block entry.
 *
 * Parameters:
 *    entry    - Path block entry for a file.
 *    buffer   - Output buffer, must have been allocated by caller.
 *
 * Return: none
 *
 */
void build_path(struct path_list_entry * entry, char * buffer);


#endif
