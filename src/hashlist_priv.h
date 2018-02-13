/*
  Copyright 2018 Jyri J. Virkki <jyri@virkki.com>

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

#ifndef _DUPD_HASHLIST_PRIV_H
#define _DUPD_HASHLIST_PRIV_H


/** ***************************************************************************
 * A hash list entry.
 *
 */
struct hash_list {
  int hash_valid;               // true if hash buffer is set to valid value
  char hash[HASH_MAX_BUFSIZE];  // the hash string shared by all these paths
  struct path_list_entry ** entries; // pointers to all paths with this hash
  int capacity;                 // 'entries' block current capacity
  int next_index;               // when adding a path, index of next one
  struct hash_list * next;      // next in list
};


/** ***************************************************************************
 * Hashtable of hash lists.
 *
 */
struct hash_table {
  struct hash_list ** table;
  uint16_t entries;
  uint8_t has_dups;
};


#endif
