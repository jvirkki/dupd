/*
  Copyright 2012-2016 Jyri J. Virkki <jyri@virkki.com>

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

#ifndef _DUPD_HASH_H
#define _DUPD_HASH_H

#include <stdint.h>


/** ***************************************************************************
 * Compute hash on one file from disk.
 *
 * The hash will be stored in the 'output' buffer provided by the
 * caller.
 *
 * Parameters:
 *    path    - The path to process. Must not be null or empty.
 *    output  - Buffer where output will be stored.
 *    blocks  - Number of blocks to read from file.
 *              Or set to 0 to read entire file.
 *    bsize   - Size of blocks to read from disk.
 *              Ignored if blocks == 0 (when reading entire file).
 *    skip    - Skip this many blocks when hashing from the file.
 *
 * Return:
 *    0 - On success
 *   -1 - If unable to compute hash (e.g. unable to open file)
 *
 */
int (*hashfn)(const char * path, char * output, uint64_t blocks,
              int bsize, uint64_t skip);


/** ***************************************************************************
 * Compute hash on data in memory.
 *
 * The hash will be stored in the 'output' buffer provided by the
 * caller.
 *
 * Parameters:
 *    buffer  - Read data from here.
 *    bufsize - Size of buffer.
 *    output  - Buffer where output will be stored.
 *
 * Return:
 *    0 - On success
 *
 */
int (*hashfn_buf)(const char * buffer, int bufsize, char * output);


#endif
