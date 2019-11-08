/*
  Copyright 2019 Jyri J. Virkki <jyri@virkki.com>

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

#include "cache.h"
#include "main.h"


/** ***************************************************************************
 * Public function, see report.h
 *
 */
void operation_cache_delete(char * path)
{
  int rv = unlink(path);
  if (rv != 0) {                                             // LCOV_EXCL_START
    char line[DUPD_PATH_MAX];
    snprintf(line, DUPD_PATH_MAX, "unlink %s", path);
    perror(line);
    exit(1);
  }                                                          // LCOV_EXCL_STOP
}
