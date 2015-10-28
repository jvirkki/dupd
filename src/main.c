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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "copying.h"
#include "dbops.h"
#include "filecompare.h"
#include "hashlist.h"
#include "main.h"
#include "paths.h"
#include "report.h"
#include "scan.h"
#include "sizelist.h"
#include "sizetree.h"
#include "stats.h"
#include "utils.h"

#define MAX_START_PATH 10

static char * operation = NULL;
static int start_path_count = 0;
static int free_start_path = 0;
static int free_db_path = 0;
static int free_file_path = 0;
int verbosity = 1;
char * start_path[MAX_START_PATH];
char * file_path = NULL;
int write_db = 1;
char * db_path = NULL;
char * cut_path = NULL;
char * exclude_path = NULL;
int exclude_path_len = 0;
unsigned int minimum_file_size = 1;
int hash_one_max_blocks = 2;
int intermediate_blocks = 0;
int hash_one_block_size = 512;
int hash_block_size = 8192;
int opt_compare_two = 1;
int opt_compare_three = 1;
long file_count = 1000000L;
int avg_path_len = 512;
int save_uniques = 0;
int have_uniques = 0;
int no_unique = 0;
char * stats_file = NULL;
int rmsh_link = 0;


/** ***************************************************************************
 * Show banner.
 *
 */
static void show_banner()
{
  printf("dupd " DUPD_VERSION " Copyright 2012-2015 Jyri J. Virkki\n");
  printf("This program comes with ABSOLUTELY NO WARRANTY.\n");
  printf("This is free software, and you are welcome to redistribute it\n");
  printf("under certain conditions. Run 'dupd license' for details.\n");
  printf("\n");
}


/** ***************************************************************************
 * Show brief usage info and exit.
 *
 */
static void show_help()
{
  show_banner();

  printf("%% dupd operation options\n");
  printf("\n");
  printf("    scan    scan starting from the given path\n");
  printf("      --path PATH         path where scanning will start\n");
  printf("      --nodb              do not generate database file\n");
  printf("      --firstblocks N     max blocks to read in first hash pass\n");
  printf("      --firstblocksize N  size of firstblocks to read\n");
  printf("      --intblocks N       blocks to read in intermediate hash\n");
  printf("      --blocksize N       size of regular blocks to read\n");
  printf("      --skip-two          do not compare two files directly\n");
  printf("      --skip-three        do not compare three files directly\n");
  printf("      --file-count        max estimated number of files to scan\n");
  printf("      --avg-size          estimated average file path length\n");
  printf("      --uniques           save info about unique files\n");
  printf("      --stats-file FILE   save stats to this file\n");
  printf("      --minsize SIZE      min size of files to scan\n");
  printf("\n");
  printf("    report  show duplicate report from last scan\n");
  printf("      --cut PATHSEG    remove 'PATHSEG' from report paths\n");
  printf("      --minsize SIZE   min size of duplicated space to report\n");
  printf("\n");
  printf("    file    based on report, check for duplicates of one file\n");
  printf("      --file PATH         check this file\n");
  printf("      --exclude-path PATH ignore duplicates under\n");
  printf("\n");
  printf("    uniques based on report, look for unique files\n");
  printf("      --path PATH         path where scanning will start\n");
  printf("      --exclude-path PATH ignore duplicates under\n");
  printf("\n");
  printf("    dups    based on report, look for duplicate files\n");
  printf("      --path PATH         path where scanning will start\n");
  printf("      --exclude-path PATH ignore duplicates under\n");
  printf("\n");
  printf("    ls      based on report, list info about every file seen\n");
  printf("      --path PATH         path where scanning will start\n");
  printf("      --exclude-path PATH ignore duplicates under\n");
  printf("\n");
  printf("    rmsh    create shell script to delete all duplicates\n");
  printf("      --link           create symlinks for deleted files\n");
  printf("      --hardlink       create hard links for deleted files\n");
  printf("\n");
  printf("    help    show brief usage info\n");
  printf("\n");
  printf("    usage   show more extensive documentation\n");
  printf("\n");
  printf("    license show license info\n");
  printf("\n");
  printf("    version show version and exit\n");
  printf("\n");
  printf("General options include:\n");
  printf("    -v          increase verbosity (may be repeated for more)\n");
  printf("    -q          quiet, supress all output except fatal errors\n");
  printf("    --db        path to dupd database file\n");
  printf("    --no-unique ignore unique table even if present\n");
  printf("\n");
  exit(0);
}


/** ***************************************************************************
 * Show built-in documentation and exit.
 * Content is compiled into the binary from the USAGE file.
 *
 */
static void show_usage()
{
  show_banner();

#ifndef __APPLE__
  char * p = &_binary_USAGE_start;
  while (p != &_binary_USAGE_end) {
    putchar(*p++);
  }
#else
  printf("Usage documentation not available on Darwin!\n");
#endif
}


/** ***************************************************************************
 * Process command line arguments and set corresponding globals.
 * Shows usage and exits if errors are detected in argument usage.
 *
 */
static void process_args(int argc, char * argv[])
{
  int i;

  if (argc < 2) {
    show_banner();
    printf("\n");
    printf("Run 'dupd help' for a summary of available options.\n");
    printf("Run 'dupd usage' for more documentation.\n");
    exit(0);
  }

  operation = argv[1];
  if (strncmp(operation, "scan", 4) &&
      strncmp(operation, "report", 6) &&
      strncmp(operation, "uniques", 7) &&
      strncmp(operation, "version", 7) &&
      strncmp(operation, "license", 7) &&
      strncmp(operation, "dups", 4) &&
      strncmp(operation, "file", 4) &&
      strncmp(operation, "ls", 2) &&
      strncmp(operation, "rmsh", 4) &&
      strncmp(operation, "usage", 5) &&
      strncmp(operation, "help", 4)) {
    printf("error: unknown operation [%s]\n", operation);
    show_help();
  }

  for (i = 2; i < argc; i++) {

    if (!strncmp(argv[i], "-v", 2)) {
      verbosity++;

    } else if (!strncmp(argv[i], "-q", 2)) {
      verbosity = -99;

    } else if (!strncmp(argv[i], "--path", 6)) {
      if (argc < i+2) { show_usage(); }
      start_path[start_path_count] = argv[i+1];
      i++;
      if (start_path[start_path_count][0] != '/') {
        printf("error: path [%s] must be absolute\n",
               start_path[start_path_count]);
        exit(1);
      }
      int x = strlen(start_path[start_path_count]) - 1;
      // Strip any trailing slashes for consistency
      while (start_path[start_path_count][x] == '/') {
        start_path[start_path_count][x--] = 0;
      }
      start_path_count++;
      if (start_path_count == MAX_START_PATH) {
        printf("error: exceeded max number of --path elements\n");
        exit(1);
      }
      start_path[start_path_count] = NULL;

    } else if (!strncmp(argv[i], "--file", 6)) {
      if (argc < i+2) { show_usage(); }
      file_path = argv[i+1];

      if (file_path[0] != '/') {
        file_path = (char *)malloc(PATH_MAX);
        free_file_path = 1;
        getcwd(file_path, PATH_MAX);
        strcat(file_path, "/");
        strcat(file_path, argv[i+1]);
      }
      i++;

    } else if (!strncmp(argv[i], "--db", 4)) {
      if (argc < i+2) { show_usage(); }
      db_path = argv[i+1];
      i++;

    } else if (!strncmp(argv[i], "--nodb", 6)) {
      write_db = 0;

    } else if (!strncmp(argv[i], "--link", 6)) {
      rmsh_link = RMSH_LINK_SOFT;

    } else if (!strncmp(argv[i], "--hardlink", 10)) {
      rmsh_link = RMSH_LINK_HARD;

    } else if (!strncmp(argv[i], "--uniques", 9)) {
      save_uniques = 1;

    } else if (!strncmp(argv[i], "--no-unique", 11)) {
      no_unique = 1;

    } else if (!strncmp(argv[i], "--skip-two", 10)) {
      opt_compare_two = 0;

    } else if (!strncmp(argv[i], "--skip-three", 12)) {
      opt_compare_three = 0;

    } else if (!strncmp(argv[i], "--intblocks", 11)) {
      if (argc < i+2) { show_usage(); }
      intermediate_blocks = atoi(argv[i+1]);
      i++;

    } else if (!strncmp(argv[i], "--firstblocksize", 16)) {
      if (argc < i+2) { show_usage(); }
      hash_one_block_size = atoi(argv[i+1]);
      i++;

    } else if (!strncmp(argv[i], "--blocksize", 11)) {
      if (argc < i+2) { show_usage(); }
      hash_block_size = atoi(argv[i+1]);
      i++;

    } else if (!strncmp(argv[i], "--firstblocks", 14)) {
      if (argc < i+2) { show_usage(); }
      hash_one_max_blocks = atoi(argv[i+1]);
      i++;

    } else if (!strncmp(argv[i], "--file-count", 12)) {
      if (argc < i+2) { show_usage(); }
      file_count = atol(argv[i+1]);
      i++;

    } else if (!strncmp(argv[i], "--avg-size", 10)) {
      if (argc < i+2) { show_usage(); }
      avg_path_len = atoi(argv[i+1]);
      i++;

    } else if (!strncmp(argv[i], "--cut", 5)) {
      if (argc < i+2) { show_usage(); }
      cut_path = argv[i+1];
      i++;

    } else if (!strncmp(argv[i], "--exclude-path", 14)) {
      if (argc < i+2) { show_usage(); }
      exclude_path = argv[i+1];
      if (exclude_path[0] != '/') {
        printf("error: --exclude-path must be absolute\n");
        exit(1);
      }
      exclude_path_len = strlen(exclude_path);
      i++;

    } else if (!strncmp(argv[i], "--stats-file", 12)) {
      if (argc < i+2) { show_usage(); }
      stats_file = argv[i+1];
      i++;

    } else if (!strncmp(argv[i], "--minsize", 9)) {
      if (argc < i+2) { show_usage(); }
      minimum_file_size = atoi(argv[i+1]);
      i++;

    } else {
      printf("error: unknown argument [%s]\n", argv[i]);
      show_usage();
    }
  }

  if (db_path == NULL) {
    db_path = (char *)malloc(PATH_MAX);
    free_db_path = 1;
    snprintf(db_path, PATH_MAX, "%s/.dupd_sqlite", getenv("HOME"));
  }

  if (start_path[0] == NULL) {
    start_path[0] = (char *)malloc(PATH_MAX);
    getcwd(start_path[0], PATH_MAX);
    start_path_count = 1;
    free_start_path = 1;
    if (verbosity >= 3) {
      printf("Defaulting --path to [%s]\n", start_path[0]);
    }
  }

  if (save_uniques && !write_db) {
    printf("error: --uniques and --nodb are incompatible\n");
    exit(1);
  }
}


/** ***************************************************************************
 * main() ;-)
 *
 */
int main(int argc, char * argv[])
{
  long t1 = get_current_time_millis();

  process_args(argc, argv);

  if (!strncmp(operation, "scan", 4)) {
    scan();

  } else if (!strncmp(operation, "report", 6)) {
    operation_report();

  } else if (!strncmp(operation, "uniques", 7)) {
    operation_uniques();

  } else if (!strncmp(operation, "license", 7)) {
    show_license();

  } else if (!strncmp(operation, "version", 7)) {
    printf(DUPD_VERSION "\n");
    exit(0);

  } else if (!strncmp(operation, "dups", 7)) {
    operation_dups();

  } else if (!strncmp(operation, "file", 4)) {
    operation_file();

  } else if (!strncmp(operation, "ls", 2)) {
    operation_ls();

  } else if (!strncmp(operation, "rmsh", 4)) {
    operation_shell_script();

  } else if (!strncmp(operation, "usage", 5)) {
    show_usage();

  } else if (!strncmp(operation, "help", 4)) {
    show_help();
  }

  if (free_file_path) { free(file_path); }
  if (free_db_path) { free(db_path); }
  if (free_start_path) { free(start_path[0]); }
  free_size_tree();
  free_path_block();
  free_hash_lists();
  free_size_list();
  free_filecompare();

  stats_time_total = get_current_time_millis() - t1;

  if (verbosity >= 3) {
    printf("Total time: %ld ms\n", stats_time_total);
  }

  if (stats_file != NULL) {
    save_stats();
  }

  // Call return() instead of exit() just to make valgrind mark as
  // an error any reachable allocations. That makes them show up
  // when running the tests.
  return(0);
}
