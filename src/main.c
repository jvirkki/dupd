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
#include "main_opt.h"
#include "optgen.h"
#include "paths.h"
#include "report.h"
#include "scan.h"
#include "sizelist.h"
#include "sizetree.h"
#include "stats.h"
#include "utils.h"

#define MAX_START_PATH 10

static int operation = -1;
static int start_path_count = 0;
static int free_db_path = 0;
static int free_file_path = 0;
int verbosity = 1;
int thread_verbosity = 0;
char * start_path[MAX_START_PATH];
char * file_path = NULL;
int write_db = 1;
char * db_path = NULL;
char * cut_path = NULL;
char * exclude_path = NULL;
int exclude_path_len = 0;
off_t minimum_file_size = 1;
int hash_one_max_blocks = 4;
int intermediate_blocks = 0;
int hash_one_block_size = 2048;
int hash_block_size = 8192;
int filecmp_block_size = 8192;
int opt_compare_two = 1;
int opt_compare_three = 1;
long file_count = 1000000L;
int avg_path_len = 512;
int save_uniques = 0;
int have_uniques = 0;
int no_unique = 0;
char * stats_file = NULL;
int rmsh_link = 0;
int scan_hidden = 0;
int path_separator = '`';
char * path_sep_string = NULL;
int x_small_buffers = 0;
int only_testing = 0;
int threaded_sizetree = 1;
int threaded_hashcompare = 1;


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
  opt_show_help();
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
  printf("\n");
  printf("Alternatively, refer to the document here:\n");
  printf("https://github.com/jvirkki/dupd/blob/master/USAGE\n");
#endif
}


/** ***************************************************************************
 * Callback called by optgen whenever a 'path' arg is seen.
 */
int opt_add_path(char * arg, int command)
{
  (void)command;

  // Strip any trailing slashes for consistency
  int x = strlen(arg) - 1;
  while (arg[x] == '/') {
    arg[x--] = 0;
  }

  // If the path is absolute just copy it as-is, otherwise prefix it with
  // the current directory.
  if (arg[0] == '/') {
    start_path[start_path_count] = (char *)malloc(x + 2);
    strcpy(start_path[start_path_count], arg);
  } else {
    start_path[start_path_count] = (char *)malloc(PATH_MAX);
    getcwd(start_path[start_path_count], PATH_MAX);
    strcat(start_path[start_path_count], "/");
    strcat(start_path[start_path_count], arg);
  }

  start_path_count++;
  if (start_path_count == MAX_START_PATH) {                  // LCOV_EXCL_START
    printf("error: exceeded max number of --path elements\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  start_path[start_path_count] = NULL;

  return OPTGEN_CALLBACK_OK;
}


/** ***************************************************************************
 * Process command line arguments and set corresponding globals.
 * Shows usage and exits if errors are detected in argument usage.
 *
 */
static void process_args(int argc, char * argv[])
{
  char * options[COUNT_OPTIONS];

  int rv = optgen_parse(argc, argv, &operation, options);

  if (options[OPT_help]) {
    show_help();
    exit(0);
  }

  if (rv == OPTGEN_NONE) {
    show_banner();
    printf("\n");
    printf("Run 'dupd help' for a summary of available options.\n");
    printf("Run 'dupd usage' for more documentation.\n");
    exit(0);
  }

  if (rv != OPTGEN_OK) {                                     // LCOV_EXCL_START
    printf("error parsing command line arguments\n");
    exit(1);
  }                                                          // LCOV_EXCL_STOP

  if (options[OPT_x_small_buffers]) { x_small_buffers = 1; }
  if (options[OPT_testing]) { only_testing = 1; }
  if (options[OPT_quiet]) { verbosity = -99; }

  verbosity += opt_count(options[OPT_verbose]);
  thread_verbosity += opt_count(options[OPT_verbose_threads]);

  path_separator = opt_char(options[OPT_pathsep], path_separator);

  if (start_path[0] == NULL) {
    start_path[0] = (char *)malloc(PATH_MAX);
    getcwd(start_path[0], PATH_MAX);
    start_path_count = 1;
    if (verbosity >= 3) {
      printf("Defaulting --path to [%s]\n", start_path[0]);
    }
  }

  if (options[OPT_file] != NULL) {
    file_path = options[OPT_file];
    // file path can be relative, normalize in that case
    if (file_path[0] != '/') {
      file_path = (char *)malloc(PATH_MAX);
      free_file_path = 1;
      getcwd(file_path, PATH_MAX);
      strcat(file_path, "/");
      strcat(file_path, options[OPT_file]);
    }
  }

  db_path = options[OPT_db];
  if (db_path == NULL) {
    db_path = (char *)malloc(PATH_MAX);
    free_db_path = 1;
    snprintf(db_path, PATH_MAX, "%s/.dupd_sqlite", getenv("HOME"));
  }

  if (options[OPT_nodb]) { write_db = 0; }
  if (options[OPT_link]) { rmsh_link = RMSH_LINK_SOFT; }
  if (options[OPT_hardlink]) { rmsh_link = RMSH_LINK_HARD; }
  if (options[OPT_uniques]) { save_uniques = 1; }
  if (options[OPT_no_unique]) { no_unique = 1; }
  if (options[OPT_skip_two]) { opt_compare_two = 0; }
  if (options[OPT_skip_three]) { opt_compare_three = 0; }
  if (options[OPT_hidden]) { scan_hidden = 1; }
  if (options[OPT_no_thread_scan]) { threaded_sizetree = 0; }
  if (options[OPT_no_thread_hash]) { threaded_hashcompare = 0; }

  intermediate_blocks = opt_int(options[OPT_intblocks], intermediate_blocks);

  hash_one_block_size =
    opt_int(options[OPT_firstblocksize], hash_one_block_size);

  hash_block_size = opt_int(options[OPT_blocksize], hash_block_size);

  filecmp_block_size = opt_int(options[OPT_fileblocksize], filecmp_block_size);

  hash_one_max_blocks = opt_int(options[OPT_firstblocks], hash_one_max_blocks);

  file_count = opt_int(options[OPT_file_count], file_count);

  avg_path_len = opt_int(options[OPT_avg_size], avg_path_len);

  cut_path = options[OPT_cut];

  exclude_path = options[OPT_exclude_path];
  if (exclude_path != NULL && exclude_path[0] != '/') {
    printf("error: --exclude-path must be absolute\n");
    exit(1);
  }

  stats_file = options[OPT_stats_file];

  minimum_file_size = opt_int(options[OPT_minsize], minimum_file_size);

  if (save_uniques && !write_db) {
    printf("error: --uniques and --nodb are incompatible\n");
    exit(1);
  }

  path_sep_string = (char *)malloc(2);
  path_sep_string[0] = (char)path_separator;
  path_sep_string[1] = 0;
}


/** ***************************************************************************
 * main() ;-)
 *
 */
int main(int argc, char * argv[])
{
  long t1 = get_current_time_millis();
  int rv = 0;
  int n;

  process_args(argc, argv);

  switch (operation) {

    case COMMAND_scan:      scan();                      break;
    case COMMAND_report:    operation_report();          break;
    case COMMAND_uniques:   operation_uniques();         break;
    case COMMAND_license:   show_license();              break;
    case COMMAND_version:   printf(DUPD_VERSION "\n");   break;
    case COMMAND_dups:      operation_dups();            break;
    case COMMAND_file:      operation_file();            break;
    case COMMAND_ls:        operation_ls();              break;
    case COMMAND_rmsh:      operation_shell_script();    break;
    case COMMAND_usage:     show_usage();                break;
    case COMMAND_help:      show_help();                 break;
    case OPTGEN_NO_COMMAND: show_help();                 rv = 1; break;

    default:                                                 // LCOV_EXCL_START
      printf("error: unknown operation [%d]\n", operation);
      rv = 1;
  }                                                          // LCOV_EXCL_STOP

  if (free_file_path) { free(file_path); }
  if (free_db_path) { free(db_path); }
  if (path_sep_string) { free(path_sep_string); }
  free_size_tree();
  free_size_list();
  free_path_block();
  free_hash_lists();
  free_filecompare();
  free_scanlist();

  for (n = 0; n < start_path_count; n++) {
    free(start_path[n]);
  }

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
  return(rv);
}
