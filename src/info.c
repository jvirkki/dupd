/*
  Copyright 2021 Jyri J. Virkki <jyri@virkki.com>

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
#include <unistd.h>

#include "info.h"
#include "utils.h"
#include "main.h"


/** ***************************************************************************
 * Show extent info for one file.
 *
 * Parameters:
 *     path - path to the file
 *
 * Return: none
 *
 */
void show_extents(char * path)
{
  struct block_list * bl;
  STRUCT_STAT finfo;

  if (get_file_info(path, &finfo)) {
    printf("error: unable to stat %s\n", path);
    return;
  }

  void * fmap = fiemap_alloc();
  bl = get_block_info_from_path(path, finfo.st_ino, finfo.st_size, fmap);
  dump_block_list("", bl);
  free(fmap);
}


/** ***************************************************************************
 * Public function, see report.h
 *
 */
void operation_info()
{
  if (info_extents_path != NULL) {
    show_extents(info_extents_path);
  }
}
