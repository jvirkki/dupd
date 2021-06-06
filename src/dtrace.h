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

#ifdef DUPD_DTRACE

#include "dupd.h"

#else

#define DTRACE_PROBE2(p, n, o, t)
#define DTRACE_PROBE3(p, n, o, t, h)

#endif // ifdef DUPD_DTRACE


#endif
