/*
  Copyright 2012-2014 Jyri J. Virkki <jyri@virkki.com>

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
#include "md5.h"
#include "stats.h"

static char * buffers[4];
static char paths[PATH_MAX * 3];


/** ***************************************************************************
 * Read from two files, comparing them.
 *
 */
static void compare_two_open_files(sqlite3 * dbh,
                                   char * path1, int file1,
                                   char * path2, int file2,
                                   off_t size, int sofar)
{
  int bread = sofar;
  ssize_t bytes1;
  ssize_t bytes2;

  while ((bytes1 = read(file1, buffers[1], HASH_BLOCK_SIZE)) > 0) {
    stats_comparison_blocks_read++;
    bread++;
    bytes2 = read(file2, buffers[2], HASH_BLOCK_SIZE);
    stats_comparison_blocks_read++;
    if ( (bytes1 != bytes2) ||
         (memcmp(buffers[1], buffers[2], bytes1)) ) {
      close(file1);
      close(file2);
      if (verbosity >= 4) {
        printf("compare_two_files: differ after reading %d blocks\n", bread);
      }
      if (save_uniques) {
        unique_to_db(dbh, path1, "2-compare");
        unique_to_db(dbh, path2, "2-compare");
      }
      return;
    }
  }

  close(file1);
  close(file2);

  if (verbosity >= 4) {
    printf("compare_two_files: duplicates after reading full files\n");
  }

  if (write_db) {
    snprintf(paths, 2 * PATH_MAX, "%s,%s", path1, path2);
    duplicate_to_db(dbh, 2, size, paths);
  }

  stats_duplicate_sets++;
  stats_duplicate_files += 2;

  if (!write_db || verbosity >= 4) {
    printf("Duplicates: file size: %ld, count: [2]\n", (long)size);
    printf(" %s\n %s\n", path1, path2);
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void compare_two_files(sqlite3 * dbh, char * path1, char * path2, off_t size)
{
  if (verbosity >= 4) {
    printf("compare_two_files: [%s] vs [%s]\n", path1, path2);
  }

  int file1 = open(path1, O_RDONLY);
  if (file1 < 0) {
    if (verbosity >= 1) {
      printf("Error opening [%s]\n", path1);
    }
    return;
  }

  int file2 = open(path2, O_RDONLY);
  if (file2 < 0) {
    if (verbosity >= 1) {
      printf("Error opening [%s]\n", path2);
    }
    close(file1);
    return;
  }

  compare_two_open_files(dbh, path1, file1, path2, file2, size, 0);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void compare_three_files(sqlite3 * dbh,
                         char * path1, char * path2, char * path3, off_t size)
{
  if (verbosity >= 4) {
    printf("compare_three_files: [%s],[%s],[%s]\n", path1, path2, path3);
  }

  int bread = 0;
  int file[4];

  file[1] = open(path1, O_RDONLY);
  if (file[1] < 0) {
    if (verbosity >= 1) {
      printf("Error opening [%s]\n", path1);
    }
    return;
  }

  file[2] = open(path2, O_RDONLY);
  if (file[2] < 0) {
    if (verbosity >= 1) {
      printf("Error opening [%s]\n", path2);
    }
    close(file[1]);
    return;
  }

  file[3] = open(path3, O_RDONLY);
  if (file[3] < 0) {
    if (verbosity >= 1) {
      printf("Error opening [%s]\n", path3);
    }
    close(file[1]);
    close(file[2]);
    return;
  }

  ssize_t bytes[4] = { 0, 0, 0, 0 };
  int i;
  int done = 0;

  while (!done) {

    for (i=1; i<4; i++) {
      if (file[i] > 0) {
        bytes[i] = read(file[i], buffers[i], HASH_BLOCK_SIZE);
        stats_comparison_blocks_read++;
      }
    }

    if (bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0) {
      done = 1;
      continue;
    }

    bread++;
    int d12 = memcmp(buffers[1], buffers[2], bytes[1]);
    int d23 = memcmp(buffers[2], buffers[3], bytes[2]);

    if (!d12 && !d23) {
      // All are the same
      continue;
    }

    int d13 = memcmp(buffers[1], buffers[3], bytes[1]);

    if (d12 && d13 && d23) {
      // All three are different, we're done here
      close(file[1]);
      close(file[2]);
      close(file[3]);
      if (verbosity >= 4) {
        printf("compare_three_files: All differ after %d blocks\n", bread);
      }
      if (save_uniques) {
        unique_to_db(dbh, path1, "3-compareALL");
        unique_to_db(dbh, path2, "3-compareALL");
        unique_to_db(dbh, path3, "3-compareALL");
      }
      return;
    }

    if (d12 && !d23) {          // 1 is different
      close(file[1]);
      if (save_uniques) {
        unique_to_db(dbh, path1, "3-compare1");
      }
      compare_two_open_files(dbh, path2, file[2], path3, file[3], size, bread);
      return;
    }

    if (!d13 && d23) {          // 2 is different
      close(file[2]);
      if (save_uniques) {
        unique_to_db(dbh, path2, "3-compare2");
      }
      compare_two_open_files(dbh, path1, file[1], path3, file[3], size, bread);
      return;
    }

    if (!d12 && d23) {          // 3 is different
      close(file[3]);
      if (save_uniques) {
        unique_to_db(dbh, path3, "3-compare3");
      }
      compare_two_open_files(dbh, path1, file[1], path2, file[2], size, bread);
      return;
    }

    printf("error: d12=%d   d23=%d   d13=%d\n", d12, d23, d13);
    exit(1);
  }

  close(file[1]);
  close(file[2]);
  close(file[3]);

  if (verbosity >= 4) {
    printf("compare_three_files: duplicates after reading full files\n");
  }

  if (write_db) {
    snprintf(paths, 3 * PATH_MAX, "%s,%s,%s", path1, path2, path3);
    duplicate_to_db(dbh, 3, size, paths);
  }

  stats_duplicate_sets++;
  stats_duplicate_files += 3;

  if (!write_db || verbosity >= 4) {
    printf("Duplicates: file size: %ld, count: [3]\n", (long)size);
    printf(" %s\n %s\n %s\n", path1, path2, path3);
  }

  return;
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void init_filecompare()
{
  buffers[1] = (char *)malloc(HASH_BLOCK_SIZE);
  buffers[2] = (char *)malloc(HASH_BLOCK_SIZE);
  buffers[3] = (char *)malloc(HASH_BLOCK_SIZE);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void free_filecompare()
{
  free(buffers[1]);
  free(buffers[2]);
  free(buffers[3]);
}
