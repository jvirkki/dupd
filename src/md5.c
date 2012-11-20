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

#include <fcntl.h>
#include <openssl/md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

#include "main.h"
#include "md5.h"
#include "stats.h"

static char buffer[HASH_BLOCK_SIZE];


/** ***************************************************************************
 * Public function, see md5.h
 *
 */
int md5(const char * path, char * out, int blocks, int skip)
{
  int counter = blocks;

  if (verbosity >= 5) {
    printf("md5: blocks=%d skip=%d path=%s\n", blocks, skip, path);
  }

  int file = open(path, O_RDONLY);
  if (file < 0) {
    if (verbosity >= 1) {
      printf("MD5: Error opening [%s]\n", path);
    }
    return(-1);
  }

  if (skip > 0) {
    lseek(file, skip * HASH_BLOCK_SIZE, SEEK_SET);
  }

  ssize_t bytes;
  MD5_CTX md5;

  MD5_Init(&md5);
  while ((bytes = read(file, buffer, HASH_BLOCK_SIZE)) > 0) {
    stats_hash_blocks_read++;
    MD5_Update(&md5, buffer, bytes);
    if (blocks) {
      counter--;
      if (counter == 0) { break; }
    }
  }

  close(file);
  MD5_Final(out, &md5);

  if (blocks == 1) { stats_single_block_count++; }
  else if (blocks > 1) { stats_mid_blocks_count++; }
  else if (blocks == 0) { stats_all_blocks_count++; }

  return(0);
}
