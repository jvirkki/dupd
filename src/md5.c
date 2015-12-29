/*
  Copyright 2012-2015 Jyri J. Virkki <jyri@virkki.com>

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
#include <inttypes.h>
#include <openssl/md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

#include "main.h"
#include "md5.h"
#include "stats.h"

#define MAX_BLOCK (1024 * 64)

static char buffer[MAX_BLOCK];


/** ***************************************************************************
 * Public function, see md5.h
 *
 */
int md5(const char * path, char * output, uint64_t blocks,
        int bsize, uint64_t skip)
{
  int counter = blocks;

  if (verbosity >= 5) {
    printf("md5: blocks(%d)=%" PRIu64 " skip=%" PRIu64 " path=%s\n",
           bsize, blocks, skip, path);
  }


  if (bsize > MAX_BLOCK) {                                   // LCOV_EXCL_START
    printf("error: md5 requested block size too big. max is %d\n", MAX_BLOCK);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  int file = open(path, O_RDONLY);
  if (file < 0) {                                            // LCOV_EXCL_START
    if (verbosity >= 1) {
      printf("MD5: Error opening [%s]\n", path);
    }
    return(-1);
  }                                                          // LCOV_EXCL_STOP

  if (skip > 0) {
    lseek(file, skip * bsize, SEEK_SET);
  }

  ssize_t bytes;
  MD5_CTX md5;

  MD5_Init(&md5);
  while ((bytes = read(file, buffer, bsize)) > 0) {
    stats_total_bytes_hashed += bytes;
    stats_total_bytes_read += bytes;
    MD5_Update(&md5, buffer, bytes);
    if (blocks) {
      counter--;
      if (counter == 0) { break; }
    }
  }

  close(file);
  MD5_Final((unsigned char *)output, &md5);

  return(0);
}
