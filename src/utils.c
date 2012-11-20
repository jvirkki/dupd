/*
  Copyright 2012 Jyri J. Virkki <jyri@virkki.com>

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

#include "main.h"
#include "utils.h"


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int file_exists(const char * path)
{
  STRUCT_STAT pathinfo;

  int rv = get_file_info(path, &pathinfo);
  if (rv < 0) {
    return 0;
  }

  if (S_ISREG(pathinfo.st_mode)) {
    return 1;
  }

  return 0;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int get_file_info(const char * path, STRUCT_STAT * info)
{
  if (path == NULL || path[0] == 0) {
    printf("get_file_info called on null or empty path!\n");
    exit(1);
  }

  int rv = LSTAT(path, info);
  if (rv) {
    if (verbosity >= 4) {
      char line[PATH_MAX];
      snprintf(line, PATH_MAX, "stat %s", path);
      perror(line);
    }
    return -1;
  }

  return 0;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
long get_current_time_millis()
{
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return (tp.tv_sec * 1000L) + (tp.tv_usec / 1000L);
}
