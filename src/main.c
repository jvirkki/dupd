/*
  Copyright 2012 Jyri J. Virkki <jyri@virkki.com>

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

#include "dbops.h"
#include "main.h"
#include "report.h"
#include "scan.h"
#include "utils.h"

static char * operation = NULL;
int verbosity = 1;
char * start_path = NULL;
int write_db = 1;
char * db_path = NULL;
char * cut_path = NULL;
int minimum_report_size = 0;
int hash_one_max_blocks = 8;
int intermediate_blocks = 0;
int opt_compare_two = 1;
int opt_compare_three = 1;
long file_count = 1000000L;
int avg_path_len = 512;


/** ***************************************************************************
 * Show brief usage info and exit.
 *
 */
static void show_usage()
{
  printf("%% dupd operation options\n");
  printf("\n");
  printf("    scan    scan starting from the given path\n");
  printf("      --path PATH      path where scanning will start\n");
  printf("      --nodb           do not generate database file\n");
  printf("      --firstblocks N  max blocks to read in first hash pass\n");
  printf("      --intblocks N    blocks to read in intermediate hash\n");
  printf("      --skip-two       do not compare two files directly\n");
  printf("      --skip-three     do not compare three files directly\n");
  printf("      --file-count     max estimated number of files to scan\n");
  printf("      --avg-size       estimated average file path length\n");
  printf("\n");
  printf("    report  show duplicate report from last scan\n");
  printf("      --cut PATHSEG    remove 'PATHSEG' from report paths\n");
  printf("      --minsize SIZE   min size of duplicated space to report\n");
  printf("\n");
  printf("    help    show more help\n");
  printf("\n");
  printf("General options include:\n");
  printf("    -v      increase verbosity (may be repeated for even more)\n");
  printf("    -q      quiet, supress all output except fatal errors\n");
  printf("    --db    path to dupd database file\n");
  printf("\n");
  exit(1);
}


/** ***************************************************************************
 * Show built-in documentation and exit.
 * Content is compiled into the binary from the USAGE file.
 *
 */
static void show_help()
{
#ifndef __APPLE__
  char * p = &_binary_USAGE_start;
  while (p != &_binary_USAGE_end) {
    putchar(*p++);
  }
#else
  printf("Usage doc not available on Darwin!\n");
#endif
  exit(0);
}


/** ***************************************************************************
 * Process commend line arguments and set corresponding globals.
 * Shows usage and exits if errors are detected in argument usage.
 *
 */
static void process_args(int argc, char * argv[])
{
  int i;

  if (argc < 2) { show_usage(); }

  operation = argv[1];
  if (strncmp(operation, "scan", 4) &&
      strncmp(operation, "report", 6) &&
      strncmp(operation, "help", 4)) {
    printf("error: unknown operation [%s]\n", operation);
    show_usage();
  }

  for (i = 2; i < argc; i++) {

    if (!strncmp(argv[i], "-v", 2)) {
      verbosity++;

    } else if (!strncmp(argv[i], "-q", 2)) {
      verbosity = -99;

    } else if (!strncmp(argv[i], "--path", 6)) {
      if (argc < i+2) { show_usage(); }
      start_path = argv[i+1];
      i++;
      if (start_path[0] != '/') {
        printf("error: path must be absolute (start with /)\n");
        exit(1);
      }

    } else if (!strncmp(argv[i], "--db", 4)) {
      if (argc < i+2) { show_usage(); }
      db_path = argv[i+1];
      i++;

    } else if (!strncmp(argv[i], "--nodb", 6)) {
      write_db = 0;

    } else if (!strncmp(argv[i], "--skip-two", 10)) {
      opt_compare_two = 0;

    } else if (!strncmp(argv[i], "--skip-three", 12)) {
      opt_compare_three = 0;

    } else if (!strncmp(argv[i], "--intblocks", 11)) {
      if (argc < i+2) { show_usage(); }
      intermediate_blocks = atoi(argv[i+1]);
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

    } else if (!strncmp(argv[i], "--minsize", 9)) {
      if (argc < i+2) { show_usage(); }
      minimum_report_size = atoi(argv[i+1]);
      i++;

    } else {
      printf("error: unknown argument [%s]\n", argv[i]);
      show_usage();
    }
  }

  if (db_path == NULL) {
    db_path = (char *)malloc(PATH_MAX);
    snprintf(db_path, PATH_MAX, "%s/.dupd_sqlite", getenv("HOME"));
  }

  if (!strncmp(operation, "scan", 4) && start_path == NULL) {
    printf("error: scan requires a start path\n");
    show_usage();
  }
}


/** ***************************************************************************
 * main() ;-)
 *
 */
int main(int argc, char * argv[])
{
  process_args(argc, argv);

  if (!strncmp(operation, "scan", 4)) {
    scan();

  } else if (!strncmp(operation, "report", 6)) {
    report();

  } else if (!strncmp(operation, "help", 4)) {
    show_help();
  }

  exit(0);
}
