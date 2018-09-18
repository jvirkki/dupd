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

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "dbops.h"
#include "filecompare.h"
#include "main.h"
#include "stats.h"
#include "utils.h"

static char * buffers[3];
static char paths[DUPD_PATH_MAX * 2];


/** ***************************************************************************
 * Read from two files, comparing them.
 *
 */
static void compare_two_open_files(sqlite3 * dbh,
                                   char * path1, int file1,
                                   char * path2, int file2,
                                   uint64_t size, int sofar)
{
  int bread = sofar;
  ssize_t bytes1;
  ssize_t bytes2;

  while ((bytes1 = read(file1, buffers[1], filecmp_block_size)) > 0) {
    stats_comparison_bytes_read += bytes1;
    stats_total_bytes_read += bytes1;
    bread++;
    bytes2 = read(file2, buffers[2], filecmp_block_size);
    stats_comparison_bytes_read += bytes2;
    stats_total_bytes_read += bytes2;
    if ( (bytes1 != bytes2) ||
         (memcmp(buffers[1], buffers[2], bytes1)) ) {
      close(file1);
      close(file2);
      LOG(L_TRACE, "compare_two_files: differ after reading %d blocks\n",
          bread);
      return;
    }
  }

  close(file1);
  close(file2);

  LOG(L_TRACE, "compare_two_files: duplicates after reading full files\n");

  snprintf(paths, 2 * DUPD_PATH_MAX, "%s%c%s", path1, path_separator, path2);
  duplicate_to_db(dbh, 2, size, paths);

  stats_duplicate_groups++;
  stats_duplicate_files += 2;

  if (log_level >= L_TRACE) {
    printf("Duplicates: file size: %ld, count: [2]\n", (long)size);
    printf(" %s\n %s\n", path1, path2);
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void compare_two_files(sqlite3 * dbh, char * path1, char * path2,
                       uint64_t size)
{
  LOG(L_TRACE, "compare_two_files: [%s] vs [%s]\n", path1, path2);

  int file1 = open(path1, O_RDONLY);
  if (file1 < 0) {
    LOG(L_PROGRESS, "Error opening [%s]\n", path1);
    return;
  }

  int file2 = open(path2, O_RDONLY);
  if (file2 < 0) {
    LOG(L_PROGRESS, "Error opening [%s]\n", path2);
    close(file1);
    return;
  }

  compare_two_open_files(dbh, path1, file1, path2, file2, size, 0);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void init_filecompare()
{
  buffers[1] = (char *)malloc(filecmp_block_size);
  buffers[2] = (char *)malloc(filecmp_block_size);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void free_filecompare()
{
  free(buffers[1]);
  free(buffers[2]);
}
