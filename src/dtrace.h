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

#ifndef _DUPD_DTRACE_H
#define _DUPD_DTRACE_H

#include <stdint.h>

#ifdef DUPD_DTRACE

#include "dupd.h"

#else // ifdef DUPD_DTRACE

#define DTRACE_PROBE2(p, n, o, t)
#define DTRACE_PROBE3(p, n, o, t, h)
#define DTRACE_PROBE4(p, n, o, t, h, f)

#endif // ifdef DUPD_DTRACE


/** ***************************************************************************
 * Common dtrace probe for file state changes. See states in paths.h
 *
 * Parameters:
 *    path   - Path to the file
 *    size   - Size of file
 *    before - Previous state
 *    after  - New state
 *
 * Return: none
 *
 */
void dtrace_set_state(char * path, uint64_t size, int before, int after);


#endif
