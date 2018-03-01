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

#ifndef _DUPD_HASHERS_H
#define _DUPD_HASHERS_H

#include <pthread.h>

#include "paths.h"

#define HASHER_QUEUE_SIZE 5000

struct hasher_param {
  int thread_num;
  int done;
  sqlite3 * dbh;
  pthread_t thread;
  pthread_mutex_t queue_lock;
  pthread_cond_t queue_cond;
  int queue_pos;
  struct path_list_head * queue[HASHER_QUEUE_SIZE];
};


/** ***************************************************************************
 * Hashes incoming file data during initial round.
 *
 * Parameters: arg points to a struct hasher_param.
 *
 * Return: none
 *
 */
void * round12_hasher(void * arg);


#endif
