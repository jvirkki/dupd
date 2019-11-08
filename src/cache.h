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

#ifndef _DUPD_CACHE_H
#define _DUPD_CACHE_H


/** ***************************************************************************
 * Delete the cache. Generally, don't do this.
 *
 * Parameters:
 *    path - path to the cache file
 *
 * Return: none
 *
 */
void operation_cache_delete(char * path);


#endif
