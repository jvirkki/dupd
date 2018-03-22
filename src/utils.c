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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "main.h"
#include "stats.h"
#include "utils.h"

#ifndef _SC_PHYS_PAGES
#define DUPD_PAGES 0
#else
#define DUPD_PAGES (uint64_t)sysconf(_SC_PHYS_PAGES)
#endif

#ifndef _SC_PAGESIZE
#define DUPD_PAGESIZE 0
#else
#define DUPD_PAGESIZE (uint64_t)sysconf(_SC_PAGESIZE)
#endif


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int file_exists(const char * path)
{
  STRUCT_STAT pathinfo;

  int rv = get_file_info(path, &pathinfo);
  if (rv < 0) { return 0; }                                  // LCOV_EXCL_LINE

  if (S_ISREG(pathinfo.st_mode)) {
    return 1;
  }

  return 0;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int get_file_info(const char * path, STRUCT_STAT * info)
{
  if (path == NULL || path[0] == 0) {                        // LCOV_EXCL_START
    printf("get_file_info called on null or empty path!\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  int rv = LSTAT(path, info);

  if (rv) {                                                  // LCOV_EXCL_START
    return -1;
  }                                                          // LCOV_EXCL_STOP

  return 0;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
long get_current_time_millis()
{
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return (tp.tv_sec * 1000L) + (tp.tv_usec / 1000L);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void time_string(char * out, int len, long ms)
{
  if (len < 13) {                                            // LCOV_EXCL_START
    printf("error: time_string buffer too small\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if (ms > 10000) {
    snprintf(out, len, "%9ld s", ms / 1000);
  } else {
    snprintf(out, len, "%8ld ms", ms);
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
long slow_down(int prob, int max_delay_ms)
{
  if (rand() % prob == 0) {
    srand((int)get_current_time_millis());
    long millis = 1 + (rand() % max_delay_ms);
    usleep(1000 * millis);
    return millis;
  }
  return 0;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
ssize_t read_file_bytes(char * path, char * output,
                        uint64_t bytes, uint64_t skip)
{
  int file = open(path, O_RDONLY);
  if (file < 0) {                                            // LCOV_EXCL_START
    LOG(L_PROGRESS, "Error opening [%s]\n", path);
    return(-1);
  }                                                          // LCOV_EXCL_STOP

  if (skip > 0) {
    uint64_t pos = lseek(file, skip, SEEK_SET);
    if (pos != skip) {                                       // LCOV_EXCL_START
      LOG(L_PROGRESS, "Error seeking [%s]\n", path);
      exit(1);
    }                                                        // LCOV_EXCL_STOP
  }

  ssize_t got = read(file, output, bytes);
  stats_total_bytes_read += got;
  close(file);
  return got;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int cpu_cores()
{
  return sysconf(_SC_NPROCESSORS_ONLN);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
uint64_t total_ram()
{
  uint64_t ram = DUPD_PAGES * DUPD_PAGESIZE;

  // If not available, just pick something semi-reasonable.
  if (ram == 0) { ram = 4L * GB1; }

  return ram;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void memdump(char * text, char * ptr, int bytes)
{
  int i;

  if (text != NULL) {
    printf("%s: ", text);
  }

  for (i=0; i<bytes; i++) {
    printf("%02x ", *((unsigned char *)ptr+i));
  }

  printf("\n");
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
char * get_thread_name()
{
  if (log_level >= L_THREADS) {
    char * name = pthread_getspecific(thread_name);
    if (name == NULL) {
      return "*** UNNAMED THREAD ***";
    } else {
      return name;
    }
  }

  return "";
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
size_t linux_strlcpy(char *dst, const char *src, size_t dstsize)
{
  size_t n = strlen(src);
  if (n >= dstsize) {                                        // LCOV_EXCL_START
    printf("error: strlcpy: src too big: %d >= %d\n", (int)n, (int)dstsize);
    exit(1);
  }                                                          // LCOV_EXCL_STOP
  strncpy(dst, src, dstsize);
  return n;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
#ifdef USE_FIEMAP
uint64_t get_first_block_open(char * path, struct fiemap * fiemap)
{
  uint64_t rv;
  int fd = open(path, O_RDONLY);

  if (fd > 0) {
    rv = get_first_block(fd, fiemap);
    close(fd);
    return rv;
  }

  return 0;
}
#endif


/** ***************************************************************************
 * Public function, see header file.
 *
 */
#ifdef USE_FIEMAP
uint64_t get_first_block(int fd, struct fiemap * fiemap)
{
  int rv;

  fiemap->fm_mapped_extents = 0;
  fiemap->fm_extent_count = 1;

  rv = ioctl(fd, FS_IOC_FIEMAP, fiemap);
  if (rv < 0) {
    LOG(L_SKIPPED, "FS_IOC_FIEMAP error, ignoring...\n");
    fiemap->fm_mapped_extents = 0;
    fiemap->fm_extents[0].fe_physical = 0;
    return 0;
  }

  if (fiemap->fm_mapped_extents == 1) {
    return fiemap->fm_extents[0].fe_physical;
  }

  return 0;
}
#endif
