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
 * For testing, slow down a bit, maybe.
 *
 * Parameters:
 *    prob         - 1 in prob chance of sleeping.
 *    max_delay_ms - Maximum delay in milliseconds.
 *
 * Return: milliseconds slept
 *
 */
long slow_down(int prob, int max_delay_ms);


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


/** ***************************************************************************
 * Read data from disk.
 *
 * Parameters:
 *    path   - Path to file to read.
 *    output - Caller-provided buffer to store the data read from disk.
 *    bytes  - Read this many bytes.
 *    skip   - Skip this many bytes (start reading file from byte skip+1).
 *
 * Return: number of bytes read (might be less than 'bytes' requested).
 *
 */
ssize_t read_file_bytes(char * path, char * output,
                        uint64_t bytes, uint64_t skip);


/** ***************************************************************************
 * Dump memory region to stdout, for debugging.
 *
 * Parameters:
 *    text  - Output prefix text.
 *    ptr   - Pointer to starting location.
 *    bytes - Print this many bytes.
 *
 * Return: none
 *
 */
void memdump(char * text, char * ptr, int bytes);


#endif
