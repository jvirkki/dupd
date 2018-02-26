/*
  Copyright 2012-2018 Jyri J. Virkki <jyri@virkki.com>

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
#include <pthread.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef linux
#define STRUCT_STAT struct stat
#define LSTAT lstat
#define strlcpy linux_strlcpy
#endif

#ifdef __OpenBSD__
#define STRUCT_STAT struct stat
#define LSTAT lstat
#endif

#ifdef __FreeBSD__
#define STRUCT_STAT struct stat
#define LSTAT lstat
#endif

#ifdef sun
#define STRUCT_STAT struct stat64
#define LSTAT lstat64
#endif

#ifdef __APPLE__
#define STRUCT_STAT struct stat
#define LSTAT lstat
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
 * Print time interval to buffer.
 *
 * Parameters:
 *    out - Output buffer.
 *    len - Size of out buffer.
 *    ms  - Time in milliseconds.
 *
 * Return: none
 *
 */
void time_string(char * out, int len, long ms);


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
 * Return number of available cores on system.
 *
 * Return: number of available cores on system.
 *
 */
int cpu_cores();


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


/** ***************************************************************************
 * Return name of this thread, only if log_level warrants using it.
 *
 * Parameters: none
 *
 * Return: thread name or empty string
 *
 */
char * get_thread_name();


/** ***************************************************************************
 * strlcpy for Linux which doesn't have it.
 *
 * Parameters:
 *    dst     - Destination string.
 *    src     - Source string.
 *    dstsize - Size of dst buffer.
 *
 * Return: bytes copied (length of src)
 *
 */
size_t linux_strlcpy(char *dst, const char *src, size_t dstsize);


/** ***************************************************************************
 * Wrapper for pthread_cond_wait, exit on failure.
 *
 * Parameters:
 *    cond  - condition variable
 *    mutex - mutex
 *
 * Return: none
 *
 */
static inline void d_cond_wait(pthread_cond_t * cond, pthread_mutex_t * mutex)
{
  int rv = pthread_cond_wait(cond, mutex);
  if (rv != 0) {                                             // LCOV_EXCL_START
    printf("error: pthread_cond_wait == %d\n", rv);
    exit(1);
  }                                                          // LCOV_EXCL_STOP
}


/** ***************************************************************************
 * Wrapper for pthread_cond_signal, exit on failure.
 *
 * Parameters:
 *    cond - condition variable
 *
 * Return: none
 *
 */
static inline void d_cond_signal(pthread_cond_t * cond)
{
  int rv = pthread_cond_signal(cond);
  if (rv != 0) {                                             // LCOV_EXCL_START
    printf("error: pthread_cond_signal == %d\n", rv);
    exit(1);
  }                                                          // LCOV_EXCL_STOP
}


/** ***************************************************************************
 * Wrapper for pthread_mutex_lock, exit on failure.
 *
 * Parameters:
 *    mutex - mutex
 *    line  - message
 *
 * Return: none
 *
 */
static inline void d_mutex_lock(pthread_mutex_t * mutex, char * line)
{
  int rv = pthread_mutex_lock(mutex);
  if (rv != 0) {                                             // LCOV_EXCL_START
    printf("error: %s: pthread_mutex_lock == %d\n", line, rv);
    exit(1);
  }                                                          // LCOV_EXCL_STOP
}


/** ***************************************************************************
 * Wrapper for pthread_mutex_unlock, exit on failure.
 *
 * Parameters:
 *    mutex
 *
 * Return: none
 *
 */
static inline void d_mutex_unlock(pthread_mutex_t * mutex)
{
  int rv = pthread_mutex_unlock(mutex);
  if (rv != 0) {                                             // LCOV_EXCL_START
    printf("error: pthread_mutex_unlock == %d\n", rv);
    exit(1);
  }                                                          // LCOV_EXCL_STOP
}


/** ***************************************************************************
 * Wrapper for pthread_create, exit on failure.
 *
 * Parameters:
 *    thread
 *    start_routine
 *
 * Return: none
 *
 */
static inline void d_create(pthread_t * thread,
                            void *(*start_routine) (void *), void * arg)
{
  int rv = pthread_create(thread, NULL, start_routine, arg);
  if (rv != 0) {                                             // LCOV_EXCL_START
    printf("error: pthread_create == %d\n", rv);
    exit(1);
  }                                                          // LCOV_EXCL_STOP
}


/** ***************************************************************************
 * Wrapper for pthread_join, exit on failure.
 *
 * Parameters:
 *    thread
 *    retval
 *
 * Return: none
 *
 */
static inline void d_join(pthread_t thread, void **retval)
{
  int rv = pthread_join(thread, retval);
  if (rv != 0) {                                             // LCOV_EXCL_START
    printf("error: pthread_join == %d\n", rv);
    exit(1);
  }                                                          // LCOV_EXCL_STOP
}

#endif
