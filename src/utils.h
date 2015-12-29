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

#ifndef _DUPD_UTILS_H
#define _DUPD_UTILS_H

#include <fcntl.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#ifdef linux
#define STRUCT_STAT struct stat
#define LSTAT lstat
#endif

#ifdef sun
#define STRUCT_STAT struct stat64
#define LSTAT lstat64
#endif

#ifdef __APPLE__
#define STRUCT_STAT struct stat64
#define LSTAT lstat64
#endif


/** ***************************************************************************
 * Convenience function to check if a given path exists and is a regular file.
 *
 * Parameters:
 *    path    - The path to check. Must not be null or empty.
 *
 * Return:
 *    1 - if it exists and is a regular file
 *    0 - otherwise
 *
 */
int file_exists(const char * path);


/** ***************************************************************************
 * Convenience function to return struct stat info for given file path.
 *
 * Parameters:
 *    path - The path to check. Must not be null or empty.
 *    info - Pointer to STRUCT_STAT allocated by caller.
 *
 * Return:
 *    0 - on success
 *   -1 - on failure
 *
 */
int get_file_info(const char * path, STRUCT_STAT * info);


/** ***************************************************************************
 * Return current time in milliseconds since epoch.
 *
 * Parameters: none
 *
 * Return: time in milliseconds
 *
 */
long get_current_time_millis();


/** ***************************************************************************
 * Compare two memory buffers similar to memcmp().
 *
 * For small buffers like the 16 bytes compared in add_hash_list(), this can
 * be faster than memcmp().
 *
 * Parameters:
 *    b1 - Pointer to first buffer
 *    b2 - Pointer to second buffer
 *    n  - Length of these buffers (number of bytes to compare)
 *
 * Return: 0 if identical, 1 if different
 *
 */
static inline int dupd_memcmp(const char * b1, const char * b2, size_t n)
{
  while (n) {
    if (*b1++ != *b2++) {
      return 1;
    }
    n--;
  }
  return 0;
}


#endif
