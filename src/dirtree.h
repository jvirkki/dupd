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

#ifndef _DUPD_DIRTREE_H
#define _DUPD_DIRTREE_H

#include <stdint.h>

struct direntry {
  uint16_t total_size;      // Total length of the path (including self 'name')
  uint16_t name_size;       // Length of the 'name' string
  char * name;              // NOT null-terminated directory name
  struct direntry * parent;
};


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
 * Build a path string given a filename and a directory entry.
 *
 * Parameters:
 *    filename - Name of the file.
 *    entry    - Directory entry of the dir where filename is located.
 *    buffer   - Output buffer, must have been allocated by caller.
 *
 * Return: none
 *
 */
void build_path(char * filename, struct direntry * entry, char * buffer);


/** ***************************************************************************
 * Debug function. Print to stdout entry and all its parents up the tree.
 *
 * Parameters:
 *    entry    - Directory entry to print.
 *
 * Return: none
 *
 */
void print_direntry(struct direntry * entry);


#endif
