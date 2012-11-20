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

#ifndef _DUPD_SIZETREE_H
#define _DUPD_SIZETREE_H


/** ***************************************************************************
 * Add the given path to the size tree. Also adds the path to the path list.
 *
 * Parameters:
 *    size - Size of this file.
 *    path - Path of this file.
 *
 * Return: none
 *
 */
void add_file(long size, char * path);


#endif
