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

#include "dtrace.h"


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void dtrace_set_state(char * path, uint64_t size, int before, int after)
{
#ifdef DUPD_DTRACE
  DTRACE_PROBE4(dupd, set_file_state, path, size, before, after);
#else
  (void)path;
  (void)size;
  (void)before;
  (void)after;
#endif
  return;
}
