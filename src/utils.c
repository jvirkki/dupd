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
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
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

  LOG(L_EVEN_MORE_TRACE, "stat: %s: size: %" PRIu64 " c_time: %" PRIu64 "\n",
      path, info->st_size, (uint64_t)info->st_ctime);

  return 0;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
uint64_t get_current_time_millis()
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
int read_file_bytes(char * path, char * output,
                    uint64_t bytes, uint64_t skip, uint64_t * bytes_read)
{
  *bytes_read = 0;

  int file = open(path, O_RDONLY);
  if (file < 0) {                                            // LCOV_EXCL_START
    LOG(L_PROGRESS, "Error opening [%s]\n", path);
    s_files_cant_read++;
    return(-1);
  }                                                          // LCOV_EXCL_STOP

  if (skip > 0) {
    uint64_t pos = lseek(file, skip, SEEK_SET);
    if (pos != skip) {                                       // LCOV_EXCL_START
      LOG(L_PROGRESS, "Error seeking [%s]\n", path);
      exit(1);
    }                                                        // LCOV_EXCL_STOP
  }

  if (bytes == 0) {
    printf("error: requested zero bytes from [%s] (skip=%" PRIu64 ")\n",
           path, skip);
    exit(1);
  }

  int rv = 0;
  ssize_t got = read(file, output, bytes);

  if (got >= 0) {
    *bytes_read = got;
    stats_total_bytes_read += *bytes_read;
  } else {
    rv = -1;
    s_files_cant_read++;
  }

  close(file);
  return rv;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int read_entry_bytes(struct path_list_entry * entry, uint64_t filesize,
                     char * path, char * output,
                     uint64_t bytes, uint64_t skip, uint64_t * bytes_read)
{
  *bytes_read = 0;
  int fd = 0;

  if (bytes == 0) {
    printf("error: requested zero bytes from [%s] (skip=%" PRIu64 ")\n",
           path, skip);
    exit(1);
  }

  if (entry->fd != 0) {
    fd = entry->fd;
  } else {
    int file = open(path, O_RDONLY);
    if (file < 0) {                                          // LCOV_EXCL_START
      LOG(L_PROGRESS, "Error opening [%s]\n", path);
      s_files_cant_read++;
      return -1;
    }                                                        // LCOV_EXCL_STOP
    fd = file;
    update_open_files(1);
    entry->file_pos = 0;
  }

  if (skip > 0) {
    if (skip != entry->file_pos) {
      uint64_t pos = lseek(fd, skip, SEEK_SET);
      if (pos != skip) {                                     // LCOV_EXCL_START
        LOG(L_PROGRESS, "Error seeking [%s]\n", path);
        exit(1);
      }                                                      // LCOV_EXCL_STOP
      entry->file_pos = pos;
    }
  }

  ssize_t got = read(fd, output, bytes);

  if (got >= 0) {
    *bytes_read = got;
    stats_total_bytes_read += *bytes_read;
    entry->file_pos += *bytes_read;
  } else {
    s_files_cant_read++;
    close(fd);
    entry->fd = 0;
    entry->file_pos = 0;
    update_open_files(-1);
    return -1;
  }

  // If all remaining files can be kept open, just do that
  int remaining = s_files_processed -
    s_files_completed_dups - s_files_completed_unique;
  if (remaining < max_open_files) {
    entry->fd = fd;
    return 0;
  }

  // If this is not the first pass (didn't start at pos 0)
  // and there are file descriptors left, keep it open
  if (skip > 0 && current_open_files < max_open_files) {
    entry->fd = fd;
    return 0;
  }

  // If file is large and there are file descriptors left,
  // keep it open.
  if (filesize > bytes && current_open_files < max_open_files) {
    entry->fd = fd;
    return 0;
  }

  update_open_files(-1);
  close(fd);
  entry->fd = 0;
  entry->file_pos = 0;

  return 0;
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
  if (ram == 0) { ram = UINT64_C(4) * GB1; }

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
      return "*** UNNAMED THREAD ***";                        // LCOV_EXCL_LINE
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
 * Create a block_list containing only one block, where "block" is inode.
 *
 */
static struct block_list * block_list_inode_only(ino_t inode, uint64_t size)
{
  struct block_list * bl = NULL;
  bl = (struct block_list *)malloc(sizeof(struct block_list) +
                                   sizeof(struct block_list_entry));
  bl->count = 1;
  bl->entry[0].start_pos = 0;
  bl->entry[0].len = size;
  bl->entry[0].block = (uint64_t)inode;
  return bl;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void dump_block_list(const char * prefix, struct block_list * bl)
{
  if (bl == NULL) { return; }

  printf("%sBLOCK LIST: count=%d\n", prefix, bl->count);
  for (int n = 0; n < bl->count; n++) {
    printf("%s[%d] "
           "start_pos: %" PRIu64 " ,  len: %" PRIu64 " , block: %" PRIu64"\n",
           prefix, n,
           bl->entry[n].start_pos, bl->entry[n].len, bl->entry[n].block);
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
struct block_list * get_block_info_from_path(char * path, ino_t inode,
                                             uint64_t size, void * map)
{
  if (using_fiemap && map == NULL) {
    printf("error: using_fiemap but no map [%s]\n", path);
    exit(1);
  }

  if (!using_fiemap) {
    return block_list_inode_only(inode, size);
  }

#ifdef USE_FIEMAP
  int rv;
  struct fiemap * fmap = (struct fiemap *)map;
  struct block_list * bl = NULL;

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    LOG(L_PROGRESS, "Unable to open [%s]\n", path);
    return block_list_inode_only(0, size);
  }

  fmap->fm_start = 0;
  fmap->fm_length = size;
  fmap->fm_flags = 0;
  fmap->fm_mapped_extents = 0;
  fmap->fm_extent_count = 255;

  rv = ioctl(fd, FS_IOC_FIEMAP, fmap);
  if (rv < 0) {
    LOG(L_SKIPPED, "%s: FS_IOC_FIEMAP error, ignoring...\n", path);
    return block_list_inode_only(0, size);
  }
  close(fd);

  if (fmap->fm_mapped_extents == 0) {
    LOG(L_SKIPPED, "%s: FS_IOC_FIEMAP returned no blocks...\n", path);
    return block_list_inode_only(0, size);
  }

  uint8_t count = 0;
  if (fmap->fm_mapped_extents < 255) {
    count = fmap->fm_mapped_extents;
  } else {
    count = 255;
  }

  bl = (struct block_list *)malloc(sizeof(struct block_list) +
                                   count * sizeof(struct block_list_entry));
  bl->count = count;
  for (int i = 0; i < count; i++) {
    bl->entry[i].start_pos = fmap->fm_extents[i].fe_logical;
    bl->entry[i].len = fmap->fm_extents[i].fe_length;
    bl->entry[i].block = fmap->fm_extents[i].fe_physical;

    // Files created recently may report back fe_physical as zero.
    // Querying the same file a bit later returns the correct block.
    // Speculating, it may be caused by the file not having been written
    // to disk yet, so it does not have any physical block yet.
    // In any case, it means we may see some fe_physical=0 even when
    // all is well. Keep track of how many.
    stats_fiemap_total_blocks++;
    if (bl->entry[i].block == 0) { stats_fiemap_zero_blocks++; }
  }

  // Correct the final block lenght so it doesn't go beyond end of file
  bl->entry[count-1].len = size - bl->entry[count-1].start_pos;

  return bl;
#endif

  return NULL;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void * fiemap_alloc()
{
#ifdef USE_FIEMAP
  struct fiemap * fiemap;
  int fiesize = sizeof(struct fiemap) + 255 * sizeof(struct fiemap_extent);

  fiemap = (struct fiemap *)malloc(fiesize);
  memset(fiemap, 0, fiesize);
  fiemap->fm_start = 0;
  fiemap->fm_length = 0;
  fiemap->fm_flags = 0;
  fiemap->fm_extent_count = 255;
  fiemap->fm_mapped_extents = 0;

  return (void *)fiemap;
#else
  return NULL;
#endif
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int get_file_limit()
{
  struct rlimit rlim;

  if (getrlimit(RLIMIT_NOFILE, &rlim)) {
    LOG(L_INFO, "Unable get file limit, guessing...\n");
    return 200;
  }

  if (rlim.rlim_cur < rlim.rlim_max) {
    rlim.rlim_cur = rlim.rlim_max;
    setrlimit(RLIMIT_NOFILE, &rlim);
    getrlimit(RLIMIT_NOFILE, &rlim);
  }

  return rlim.rlim_cur;
}
