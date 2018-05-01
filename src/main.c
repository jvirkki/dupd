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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "copying.h"
#include "filecompare.h"
#include "hash.h"
#include "hashlist.h"
#include "main.h"
#include "man.h"
#include "optgen.h"
#include "paths.h"
#include "readlist.h"
#include "refresh.h"
#include "report.h"
#include "scan.h"
#include "sizelist.h"
#include "sizetree.h"
#include "stats.h"
#include "testing.h"
#include "utils.h"

#define MAX_START_PATH 10
#define START_PATH_NULL 0
#define START_PATH_GIVEN 1
#define START_PATH_ERROR 2

static int operation = -1;
static int start_path_count = 0;
static int start_path_state = 0;
static int free_db_path = 0;
static int free_file_path = 0;
int log_level = 1;
int log_only = 0;
char * start_path[MAX_START_PATH];
char * file_path = NULL;
int write_db = 1;
char * db_path = NULL;
char * cut_path = NULL;
char * exclude_path = NULL;
int exclude_path_len = 0;
uint32_t minimum_file_size = 1;
int hash_one_max_blocks = 16;
uint32_t hash_one_block_size = 0;
uint32_t round1_max_bytes = 0;
uint32_t DEF_HDD_hash_one_block_size = 1024*128;
uint32_t DEF_SSD_hash_one_block_size = 1024*16;
int hash_block_size = 8192;
int filecmp_block_size = 131072;
int opt_compare_two = 1;
int opt_compare_three = 1;
int save_uniques = 0;
int have_uniques = 0;
int no_unique = 0;
char * stats_file = NULL;
int rmsh_link = 0;
int scan_hidden = 0;
int path_separator = '\x1C';
char * path_sep_string = NULL;
int x_small_buffers = 0;
int only_testing = 0;
int hdd_mode = 1;
int threaded_sizetree = 1;
int hardlink_is_unique = 0;
int hash_function = -1;
int hash_bufsize = -1;
long db_warn_age_seconds = 60 * 60 * 24 * 3; /* 3 days */
int report_format = REPORT_FORMAT_TEXT;
pthread_key_t thread_name;
pthread_mutex_t logger_lock = PTHREAD_MUTEX_INITIALIZER;
int sort_bypass = 0;
uint64_t buffer_limit = 0;
int one_file_system = 0;
int using_fiemap = 0;

char * log_level_name[] = {
  "NONE",
  "BASE",
  "MORE",
  "PROGRESS",
  "INFO",
  "MORE_INFO",
  "RESOURCES",
  "THREADS",
  "SKIPPED",
  "MORE_THREADS",
  "TRACE ",
  "FILES ",
  "MORE_TRACE ",
};


/** ***************************************************************************
 * Show banner.
 *
 */
static void show_banner()
{
  printf("dupd " DUPD_VERSION " Copyright 2012-2018 Jyri J. Virkki\n");
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
 * Content is compiled into the binary from the manpage.
 *
 */
static void show_usage()
{
  show_banner();

  for (unsigned int c = 0; c < man_dupd_len; c++) {
    putchar((char)man_dupd[c]);
  }
}


/** ***************************************************************************
 * Callback called by optgen whenever a 'path' arg is seen.
 */
int opt_add_path(char * arg, int command)
{
  (void)command;

  // Mark that path option was given at least once
  if (start_path_state == START_PATH_NULL) {
    start_path_state = START_PATH_GIVEN;
  }

  // Strip any trailing slashes for consistency
  int x = strlen(arg) - 1;
  if (x > 0) {
    while (arg[x] == '/') {
      arg[x--] = 0;
    }
  }

  // If the path is absolute just copy it as-is, otherwise prefix it with
  // the current directory.
  if (arg[0] == '/') {
    start_path[start_path_count] = (char *)malloc(x + 2);
    strcpy(start_path[start_path_count], arg);
  } else {
    start_path[start_path_count] = (char *)malloc(DUPD_PATH_MAX);
    getcwd(start_path[start_path_count], DUPD_PATH_MAX);
    strcat(start_path[start_path_count], "/");
    strcat(start_path[start_path_count], arg);
  }

  STRUCT_STAT info;
  if (get_file_info(start_path[start_path_count], &info) ||
      !S_ISDIR(info.st_mode)) {
    printf("error: not a directory: %s\n", start_path[start_path_count]);
    free(start_path[start_path_count]);
    start_path[start_path_count] = NULL;
    start_path_state = START_PATH_ERROR;
    return OPTGEN_CALLBACK_OK;
  }

  for (int n = 0; n < start_path_count; n++) {
    if (strstr(start_path[n], start_path[start_path_count]) ||
        strstr(start_path[start_path_count], start_path[n])) {
      printf("error: overlap between %s and %s\n",
             start_path[n], start_path[start_path_count]);
      free(start_path[start_path_count]);
      start_path[start_path_count] = NULL;
      start_path_state = START_PATH_ERROR;
      return OPTGEN_CALLBACK_OK;
    }
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
 * Free any allocated start_path entries.
 *
 */
static void free_start_paths()
{
  start_path_count = 0;
  for (int n = 0; n < MAX_START_PATH; n++) {
    if (start_path[n]) {
      free(start_path[n]);
      start_path[n] = NULL;
    }
  }
}


/** ***************************************************************************
 * Process command line arguments and set corresponding globals.
 * Shows usage and exits if errors are detected in argument usage.
 *
 */
static int process_args(int argc, char * argv[])
{
  char * options[COUNT_OPTIONS];
  uint64_t user_ram_limit = 0;

#ifdef USE_FIEMAP
  using_fiemap = 1;
#endif

  int rv = optgen_parse(argc, argv, &operation, options);

  if (options[OPT_help]) {
    show_help();
    return 1;
  }

  if (rv == OPTGEN_NONE) {
    show_banner();
    printf("\n");
    printf("Run 'dupd help' for a summary of available options.\n");
    printf("Run 'dupd usage' for more documentation.\n");
    return 1;
  }

  if (rv != OPTGEN_OK) {                                     // LCOV_EXCL_START
    printf("error parsing command line arguments\n");
    return 2;
  }                                                          // LCOV_EXCL_STOP

  if (options[OPT_cmp_two] && options[OPT_skip_two]) {
    printf("error: unable to both skip and compare two!\n");
    return 2;
  }

  if (options[OPT_cmp_three] && options[OPT_skip_three]) {
    printf("error: unable to both skip and compare three!\n");
    return 2;
  }

  if (options[OPT_x_small_buffers]) { x_small_buffers = 1; }
  if (options[OPT_x_testing]) { only_testing = 1; }
  if (options[OPT_quiet]) { log_level = -99; }
  log_level = opt_int(options[OPT_verbose_level], log_level);
  log_level += opt_count(options[OPT_verbose]);
  if (log_level > L_MAX_LOG_LEVEL) { log_level = L_MAX_LOG_LEVEL; }

  if (options[OPT_log_only]) { log_only = 1; }

  path_separator = opt_char(options[OPT_pathsep], path_separator);

  if (start_path_state == START_PATH_NULL) {
    start_path[0] = (char *)malloc(DUPD_PATH_MAX);
    getcwd(start_path[0], DUPD_PATH_MAX);
    start_path_count = 1;
    LOG(L_INFO, "Defaulting --path to [%s]\n", start_path[0]);
  }

  if (options[OPT_file] != NULL) {
    file_path = options[OPT_file];
    // file path can be relative, normalize in that case
    if (file_path[0] != '/') {
      file_path = (char *)malloc(DUPD_PATH_MAX);
      free_file_path = 1;
      getcwd(file_path, DUPD_PATH_MAX);
      strcat(file_path, "/");
      strcat(file_path, options[OPT_file]);
    }
  }

  db_path = options[OPT_db];
  if (db_path == NULL) {
    db_path = (char *)malloc(DUPD_PATH_MAX);
    free_db_path = 1;
    snprintf(db_path, DUPD_PATH_MAX, "%s/.dupd_sqlite", getenv("HOME"));
  }

  if (options[OPT_ssd]) { hdd_mode = 0; }
  if (options[OPT_hdd]) { hdd_mode = 1; }
  if (options[OPT_nodb]) { write_db = 0; }
  if (options[OPT_link]) { rmsh_link = RMSH_LINK_SOFT; }
  if (options[OPT_hardlink]) { rmsh_link = RMSH_LINK_HARD; }
  if (options[OPT_uniques]) { save_uniques = 1; }
  if (options[OPT_no_unique]) { no_unique = 1; }
  if (options[OPT_skip_two]) { opt_compare_two = 0; }
  if (options[OPT_skip_three]) { opt_compare_three = 0; }
  if (options[OPT_hidden]) { scan_hidden = 1; }
  if (options[OPT_no_thread_scan]) { threaded_sizetree = 0; }
  if (options[OPT_hardlink_is_unique]) { hardlink_is_unique = 1; }
  if (options[OPT_one_file_system]) { one_file_system = 1; }

  hash_one_block_size = opt_int(options[OPT_firstblocksize],
                                hash_one_block_size);

  hash_block_size = opt_int(options[OPT_blocksize], hash_block_size);

  filecmp_block_size = opt_int(options[OPT_fileblocksize], filecmp_block_size);

  hash_one_max_blocks = opt_int(options[OPT_firstblocks], hash_one_max_blocks);

  cut_path = options[OPT_cut];

  exclude_path = options[OPT_exclude_path];
  if (exclude_path != NULL && exclude_path[0] != '/') {
    printf("error: --exclude-path must be absolute\n");
    return 2;
  }

  stats_file = options[OPT_stats_file];

  minimum_file_size = opt_int(options[OPT_minsize], minimum_file_size);
  if (minimum_file_size < 1) { minimum_file_size = 1; }

  if (save_uniques && !write_db) {
    printf("error: --uniques and --nodb are incompatible\n");
    return 2;
  }

  path_sep_string = (char *)malloc(2);
  path_sep_string[0] = (char)path_separator;
  path_sep_string[1] = 0;

  char * hash_name = opt_string(options[OPT_hash], "xxhash");
  if (!strcmp("md5", hash_name)) {
    hash_function = HASH_FN_MD5;
  } else if (!strcmp("sha1", hash_name)) {
    hash_function = HASH_FN_SHA1;
  } else if (!strcmp("sha512", hash_name)) {
    hash_function = HASH_FN_SHA512;
  } else if (!strcmp("xxhash", hash_name)) {
    hash_function = HASH_FN_XXHASH;
  } else {
    printf("error: unknown hash %s\n", hash_name);
    return 2;
  }
  hash_bufsize = hash_get_bufsize(hash_function);

  char * report_format_name = opt_string(options[OPT_format], "text");
  if (!strcmp("text", report_format_name)) {
    report_format = REPORT_FORMAT_TEXT;
  } else if (!strcmp("csv", report_format_name)) {
    report_format = REPORT_FORMAT_CSV;
  } else if (!strcmp("json", report_format_name)) {
    report_format = REPORT_FORMAT_JSON;
  } else {
    printf("error: unknown report format %s\n", report_format_name);
    return 2;
  }

  char * buflimstr = opt_string(options[OPT_buflimit], "0");
  if (strcmp("0", buflimstr)) {
    int len = strlen(buflimstr);
    if (buflimstr[len-1] == 'M') {
      user_ram_limit = MB1;
      buflimstr[len-1] = 0;
    } else if (buflimstr[len-1] == 'G') {
      user_ram_limit = GB1;
      buflimstr[len-1] = 0;
    } else {
      user_ram_limit = 1;
    }
    long c = atol(buflimstr);
    user_ram_limit *= c;

    if (user_ram_limit < MB8) {
      user_ram_limit = MB8;
    }
  }

  if (hdd_mode) {
    opt_compare_two = 0;
    opt_compare_three = 0;
    if (options[OPT_cmp_two]) {
      opt_compare_two = 1;
    }
    if (options[OPT_cmp_three]) {
      opt_compare_three = 1;
    }
  }

  if (hash_one_block_size == 0) {
    if (hdd_mode) {
      hash_one_block_size = DEF_HDD_hash_one_block_size;
    } else {
      hash_one_block_size = DEF_SSD_hash_one_block_size;
    }
  }

  round1_max_bytes = hash_one_block_size * hash_one_max_blocks;

  if (options[OPT_ssd] && options[OPT_hdd]) {
    printf("error: SSD mode and HDD mode are mutually exclusive\n");
    return 2;
  }

  char * sortby = opt_string(options[OPT_sort_by], "def");
  if (!strcmp("inode", sortby)) {
    sort_bypass = SORT_BY_INODE;
  } else if (!strcmp("block", sortby)) {
    sort_bypass = SORT_BY_BLOCK;
  } else if (!strcmp("none", sortby)) {
    sort_bypass = SORT_BY_NONE;
  }
  if (sort_bypass != 0) {
    LOG(L_INFO, "Sort bypass set to %s\n", sortby);
    if (hardlink_is_unique) {
      printf("Don't do that..\n");
      return 2;
    }
  }

  if (!hdd_mode) { using_fiemap = 0; }
  if (sort_bypass != 0 && sort_bypass != SORT_BY_BLOCK) {
    using_fiemap = 0;
  }

  if (options[OPT_x_nofie]) { using_fiemap = 0; }

  LOG(L_INFO, "Will be using_fiemap (if available): %d\n", using_fiemap);

  uint64_t ram = total_ram();
  if (user_ram_limit > 0) {
    if (user_ram_limit > ram) {
      buffer_limit = 0.9 * ram;
    } else {
      buffer_limit = user_ram_limit;
    }
  }

  if (buffer_limit == 0) {
    buffer_limit = 0.6 * ram;
    if (x_small_buffers) {
      buffer_limit = 4 * MB1;
    }
  }

  int ramm = ram / (1024 * 1024);
  int blim = buffer_limit / (1024 * 1024);
  LOG(L_INFO, "Reported RAM: %dMB  buffer limit: %dMB\n", ramm, blim);

  return 0;
}


/** ***************************************************************************
 * main() ;-)
 *
 */
int main(int argc, char * argv[])
{
  stats_main_start = get_current_time_millis();
  int rv = 0;

  pthread_key_create(&thread_name, NULL);
  pthread_setspecific(thread_name, (char *)"[MAIN] ");

  rv = process_args(argc, argv);

  LOG(L_PROGRESS, "Log level: %s\n", log_level_name[log_level]);

  // If process_args returns non-zero it means we need to exit right away
  // with an exit code one less than the returned value. Need to exit via
  // the DONE section both to free any memory that may have been allocated
  // already and also to properly return (not exit) from main (see below).
  if (rv) {
    rv--;
    goto DONE;
  }

  // If bad --path values given, don't try to process them. Arguably one
  // could process the good ones (if any) but better to flag the path
  // error up front instead of spending time doing a partial scan.
  if (start_path_state == START_PATH_ERROR) {
    rv = 1;
    goto DONE;
  }

  LOG(L_INFO, "Claimed CPU cores: %d\n", cpu_cores());

  switch (operation) {

    case COMMAND_scan:      scan();                      break;
    case COMMAND_refresh:   operation_refresh();         break;
    case COMMAND_report:    operation_report();          break;
    case COMMAND_uniques:   operation_uniques();         break;
    case COMMAND_license:   show_license();              break;
    case COMMAND_version:   printf(DUPD_VERSION "\n");   break;
    case COMMAND_dups:      operation_dups();            break;
    case COMMAND_file:      operation_file();            break;
    case COMMAND_ls:        operation_ls();              break;
    case COMMAND_rmsh:      operation_shell_script();    break;
    case COMMAND_validate:  rv = operation_validate();   break;
    case COMMAND_usage:     show_usage();                break;
    case COMMAND_man:       show_usage();                break;
    case COMMAND_help:      show_help();                 break;
    case COMMAND_testing:   testing();                   break;
    case OPTGEN_NO_COMMAND: show_help();                 rv = 1; break;

    default:                                                 // LCOV_EXCL_START
      printf("error: unknown operation [%d]\n", operation);
      rv = 1;
  }                                                          // LCOV_EXCL_STOP

 DONE:
  if (free_file_path) { free(file_path); }
  if (free_db_path) { free(db_path); }
  if (path_sep_string) { free(path_sep_string); }
  free_size_tree();
  free_size_list();
  free_path_block();
  free_filecompare();
  free_scanlist();
  free_start_paths();
  free_read_list();
  free_dirtree();
  free_hashlist();

  stats_time_total = get_current_time_millis() - stats_main_start;

  LOG(L_PROGRESS, "Total time: %ld ms\n", stats_time_total);

  if (stats_file != NULL) {
    save_stats();
  }

  if (log_level >= 0) {
    if (operation == COMMAND_scan ||
        operation == COMMAND_refresh || operation == COMMAND_license ||
        operation == COMMAND_version || operation == COMMAND_validate ||
        operation == COMMAND_usage || operation == COMMAND_man ||
        operation == COMMAND_help) {

      if (!strcmp("dev", DUPD_VERSION + strlen(DUPD_VERSION) - 3)) {
        if (isatty(fileno(stdout))) {
          fprintf(stdout, "\nNote: This is a development version of dupd ("
                  DUPD_VERSION ") (" GITHASH ")\n");
          fprintf(stdout,
                  "May contain known bugs or unstable work in progress!\n");
          fprintf(stdout,
                  "If stability is desired, use a release version of dupd.\n");
        }
      }
    }
  }

  // Call return() instead of exit() just to make valgrind mark as
  // an error any reachable allocations. That makes them show up
  // when running the tests.
  return(rv);
}
