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

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "dbops.h"
#include "dirtree.h"
#include "main.h"
#include "paths.h"
#include "scan.h"
#include "sizetree.h"
#include "stats.h"
#include "utils.h"

struct size_node {
  uint64_t size;
  struct path_list_head * paths;
  struct size_node * left;
  struct size_node * right;
  char * filename;
  struct direntry * dir_entry;
};

static struct size_node * tip = NULL;

struct stat_queue {
  uint64_t block;
  int end;
  dev_t device;
  ino_t inode;
  uint64_t size;
  struct direntry * dir_entry;
  char filename[DUPD_FILENAME_MAX];
  char path[DUPD_PATH_MAX];
  struct stat_queue * next;
};

#define QUEUE_COUNT 4
static struct stat_queue queue[QUEUE_COUNT];
#define STAT_QUEUE_LENGTH 50

#define PRODUCER_DONE 42
#define WORKER_NOT_STARTED 43
#define WORKER_DONE 44
#define WORKING 45
#define WANT_QUEUE 46
#define PRODUCER 47
#define WORKER 48
#define NOBODY 49

static int current_producer_queue;
static int current_worker_queue;
static int last_owner[QUEUE_COUNT];
static struct stat_queue * producer_next;
static long queue_added[QUEUE_COUNT];
static long queue_removed[QUEUE_COUNT];

static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_producer_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t queue_worker_cond = PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;

#define PRINT_INFO  if (log_level >= L_TRACE) {                         \
    LOG(L_TRACE, "current_worker_queue=%s, current_producer_queue=%s\n", \
        queue_state(current_worker_queue),                              \
        queue_state(current_worker_queue));                             \
  }


/** ***************************************************************************
 * Return string description of a queue state.
 *
 */
static char * queue_state(int s)
{
  switch (s) {
  case PRODUCER_DONE:      return "PRODUCER_DONE";
  case WORKER_NOT_STARTED: return "WORKER_NOT_STARTED";
  case WORKER_DONE:        return "WORKER_DONE";
  case WORKING:            return "WORKING";
  case WANT_QUEUE:         return "WANT_QUEUE";
  case 0: return "Q0";
  case 1: return "Q1";
  case 2: return "Q2";
  case 3: return "Q3";
  default:
    printf("Bad queue state %d\n", s);
    exit(1);
  }
}


/** ***************************************************************************
 * Allocate a new size tree leaf node to store the given size and path.
 *
 * Parameters:
 *    size      - Size of this file.
 *    filename  - Name of the file (no path).
 *    dir_entry - Directory where filename lives.
 *
 * Return: ptr to the node created
 *
 */
static struct size_node * new_node(uint64_t size, char * filename,
                                   struct direntry * dir_entry)
{
  struct size_node * n = (struct size_node *)malloc(sizeof(struct size_node));
  n->left = NULL;
  n->right = NULL;
  n->size = size;
  n->paths = NULL;
  n->dir_entry = dir_entry;
  int l = strlen(filename);
  n->filename = (char *)malloc(l + 1);
  strlcpy(n->filename, filename, l + 1);

  if (debug_size == size) {
    LOG(L_PROGRESS, "new_node: size tree node created for size %" PRIu64
        " by file [%s]\n", size, filename);
  }

  return n;
}


/** ***************************************************************************
 * Add the given size and path below the given node.
 * If no matching size node is found, create a new_node().
 *
 * Parameters:
 *    size - Size of this file.
 *    path - Path of this file.
 *
 * Return: none
 *
 */
static void add_below(struct size_node * node, ino_t inode,
                      uint64_t size, char * filename,
                      struct direntry * dir_entry)
{
  struct size_node * p = node;

  s_files_in_sizetree++;

  while (1) {

    if (size == p->size) {
      // The first file of this size is kept in the size_node itself,
      // waiting to see if another file of the same size is found.
      // If we reached a size_node which exists but paths is NULL that
      // means we just found the second file of this size. So it's time
      // to add both the previous and current to the path list.

      if (p->paths == NULL) {
        p->paths = insert_first_path(p->filename, p->dir_entry, size);
        p->dir_entry = NULL;
        free(p->filename);
        p->filename = NULL;
      }

      insert_end_path(filename, dir_entry, inode, size, p->paths);

      return;
    }

    if (size > p->size) {
      if (p->left == NULL) {
        p->left = new_node(size, filename, dir_entry);
        return;
      } else {
        p = p->left;
      }
    } else {
      if (p->right == NULL) {
        p->right = new_node(size, filename, dir_entry);
        return;
      } else {
        p = p->right;
      }
    }
  }
}


/** ***************************************************************************
 * Worker thread for inserting files to the sizetree if using threaded scan.
 *
 * The scan operation queues files via add_queue() into its queue and this
 * worker thread reads them from its queue to add them via add_file(). The
 * scanner and this worker exchange queues as they are done with them.
 *
 * Parameters:
 *    arg  - Unused.
 *
 * Return: none
 *
 */
static void * worker_main(void * arg)
{
  (void)arg;
  int done = 0;
  char * self = "                                        [sizetree] ";
  struct stat_queue * worker_next;
  int want_queue = 0;

  pthread_setspecific(thread_name, self);
  LOG(L_THREADS, "Thread created\n");

  while (!done) {

    // Before working through a queue, need to get one first.
    pthread_mutex_lock(&queue_lock);

    while (want_queue == current_producer_queue ||
           last_owner[want_queue] == WORKER) {
      LOG_MORE_THREADS {
        LOG(L_MORE_THREADS, "Want Q%d, not available, WAIT\n", want_queue);
        PRINT_INFO;
      }
      pthread_cond_signal(&queue_producer_cond);
      pthread_cond_wait(&queue_worker_cond, &queue_lock);
    }

    current_worker_queue = want_queue;

    if (last_owner[current_worker_queue] != PRODUCER) {      // LCOV_EXCL_START
      printf("worker got Q%d but last_owner=%d\n",
             current_worker_queue, last_owner[current_worker_queue]);
      exit(1);
    }                                                        // LCOV_EXCL_STOP

    last_owner[current_worker_queue] = WORKER;

    LOG_MORE_THREADS {
      LOG(L_MORE_THREADS, "Took Q%d\n", current_worker_queue);
      PRINT_INFO;
    }

    pthread_mutex_unlock(&queue_lock);

    // Work through the current queue to its end (or to the path marked end)

    LOG_MORE_THREADS {
      LOG(L_MORE_THREADS, "Processing Q%d\n", current_worker_queue);
      PRINT_INFO;
    }

    worker_next = &queue[current_worker_queue];

    while (!done && worker_next != NULL) {
      if (worker_next->end) {
        LOG_THREADS {
          LOG(L_THREADS, "Got END flag: DONE\n");
          PRINT_INFO;
        }
        done = 1;

      } else {
        add_file(NULL, worker_next->inode,
                 worker_next->size, worker_next->path,
                 worker_next->filename, worker_next->dir_entry);
        queue_removed[current_worker_queue]++;
        worker_next = worker_next->next;
      }
    }

    LOG_MORE_THREADS {
      LOG(L_MORE_THREADS, "Finished queue %d, removed from it %ld so far\n",
          current_worker_queue, queue_removed[current_worker_queue]);
      PRINT_INFO;
    }

    if (only_testing) {
      slow_down(10, 100);
      slow_down(100, 1000);
    }

    if (!done) {
      want_queue = (current_worker_queue + 1) % QUEUE_COUNT;

      pthread_mutex_lock(&queue_lock);

      if (queue_added[current_worker_queue] !=               // LCOV_EXCL_START
          queue_removed[current_worker_queue]) {
        printf("Q%d: added (%ld) != removed (%ld)\n",
               current_worker_queue,
               queue_added[current_worker_queue],
               queue_removed[current_worker_queue]);
        exit(1);
      }                                                      // LCOV_EXCL_STOP

      current_worker_queue = WANT_QUEUE;
      pthread_cond_signal(&queue_producer_cond);
      pthread_mutex_unlock(&queue_lock);
    }
  }

  LOG(L_THREADS, "Thread finished\n");

  return(NULL);
}


/** ***************************************************************************
 * Free one node and its children.
 *
 */
static void free_node(struct size_node * node)
{
  if (node->left != NULL) { free_node(node->left); }
  if (node->right != NULL) { free_node(node->right); }
  if (node->filename != NULL) { free(node->filename); }
  free(node);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int add_file(sqlite3 * dbh,
             ino_t inode, uint64_t size,  char * path,
             char * filename, struct direntry * dir_entry)
{
  (void)dbh;                    /* not used */
  static STRUCT_STAT new_stat_info;

  LOG(L_FILES, "FILE: [%s]\n", path);

  // If size is SCAN_SIZE_UNKNOWN, it means the producer thread did not
  // stat() the file during scan, so we'll need to do it now.

  if (size == SCAN_SIZE_UNKNOWN) {

    int rv = get_file_info(path, &new_stat_info);
    if (rv != 0) {
      LOG(L_PROGRESS, "SKIP (error) [%s]\n", path);
      stats_files_error++;
      return(-2);
    }

    size = new_stat_info.st_size;
    inode = new_stat_info.st_ino;

    if (debug_size == size) {
      LOG(L_PROGRESS, "add_file: SCAN_SIZE_UNKNOWN resolved to %" PRIu64
          " for [%s]\n", size, path);
    }
  }

  if (size < minimum_file_size) {
    LOG(L_TRACE, "SKIP (too small: %" PRIu64 "): [%s]\n", size, path);
    s_files_too_small++;
    return(-2);
  }

  if (tip == NULL) {
    tip = new_node(size, filename, dir_entry);
    s_files_in_sizetree = 1;
    return(-2);
  }

  add_below(tip, inode, size, filename, dir_entry);

  return(-2);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int add_queue(sqlite3 * dbh,
              ino_t inode, uint64_t size, char * path,
              char * filename, struct direntry * dir_entry)
{
  (void)dbh;                    /* not used */

  LOG(L_MORE_TRACE, "add_queue (%d): %s\n", current_producer_queue, path);

  if (debug_size == size) {
    LOG(L_PROGRESS, "add_queue (%d): %s\n", current_producer_queue, path);
  }

  // Just add it to the end of the queue producer currently owns.
  producer_next->size = size;
  producer_next->inode = inode;
  strlcpy(producer_next->filename, filename, DUPD_FILENAME_MAX);
  producer_next->dir_entry = dir_entry;
  strlcpy(producer_next->path, path, DUPD_PATH_MAX);
  producer_next = producer_next->next;
  queue_added[current_producer_queue]++;

  // Unless we've hit the end of this queue, that's all for now...
  if (producer_next != NULL) {
    return(-2);
  }

  if (only_testing) {
    slow_down(10, 100);
    slow_down(100, 1000);
  }

  // If we did reach the end of this queue, need to move to the next one.
  LOG_MORE_THREADS {
    LOG(L_MORE_THREADS,
        "Finished filling Q%d\n", current_producer_queue);
    PRINT_INFO;
  }

  int want = (current_producer_queue + 1) % QUEUE_COUNT;

  pthread_mutex_lock(&queue_lock);

  current_producer_queue = WANT_QUEUE;

  while (want == current_worker_queue || last_owner[want] == PRODUCER) {
    LOG_MORE_THREADS {
      LOG(L_MORE_THREADS, "Want Q%d, not available, WAIT\n", want);
      PRINT_INFO;
    }
    pthread_cond_signal(&queue_worker_cond);
    pthread_cond_wait(&queue_producer_cond, &queue_lock);
  }

  current_producer_queue = want;
  last_owner[current_producer_queue] = PRODUCER;

  pthread_cond_signal(&queue_worker_cond);
  pthread_mutex_unlock(&queue_lock);

  producer_next = &queue[current_producer_queue];

  LOG_MORE_THREADS {
    LOG(L_MORE_THREADS, "Took Q%d\n", current_producer_queue);
    PRINT_INFO;
  }

  return(-2);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void scan_done()
{
  pthread_mutex_lock(&queue_lock);

  LOG_THREADS {
    LOG(L_THREADS,
        "scan_done: END in %s\n", queue_state(current_producer_queue));
    PRINT_INFO;
  }

  producer_next->end = 1;       // tells worker this will the last entry
  current_producer_queue = PRODUCER_DONE;

  pthread_cond_signal(&queue_worker_cond);
  pthread_mutex_unlock(&queue_lock);

  LOG(L_THREADS, "Waiting for sizetree worker thread to finish...\n");

  d_join(worker_thread, NULL);

  // Verify counts for sanity checking...
  uint32_t removed = 0;
  uint32_t added = 0;
  for (int i = 0; i < QUEUE_COUNT; i++) {
    LOG(L_MORE_THREADS, "Q%d: added %ld, removed %ld\n",
        i, queue_added[i], queue_removed[i]);
    removed += queue_removed[i];
    added += queue_added[i];
  }

  LOG(L_MORE_THREADS,
      "Total added %" PRIu32 ", removed %" PRIu32 "\n", added, removed);

                                                             // LCOV_EXCL_START
  if (added != removed) {
    printf("added (%" PRIu32 ") != removed (%" PRIu32 ")\n", added, removed);
    exit(1);
  }                                                          // LCOV_EXCL_STOP

}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void init_sizetree()
{
  int i;
  int n;
  struct stat_queue * p;

  if (threaded_sizetree) {
    for (i = 0; i < QUEUE_COUNT; i++) {
      queue_removed[i] = 0;
      queue_added[i] = 0;
      last_owner[i] = NOBODY;
      p = &queue[i];
      for (n = 1; n < STAT_QUEUE_LENGTH; n++) {
        p->next = (struct stat_queue *)malloc(sizeof(struct stat_queue));
        p->next->end = 0;
        p = p->next;
        p->next = NULL;
      }
    }

    // Assign Q0 to producer initially.
    current_producer_queue = 0;
    producer_next = &queue[0];
    last_owner[0] = PRODUCER;

    // Worker can't start yet.
    current_worker_queue = WORKER_NOT_STARTED;

    if (pthread_create(&worker_thread, NULL, worker_main, NULL)) {
                                                             // LCOV_EXCL_START
      printf("error: unable to create sizetree worker thread!\n");
      exit(1);
    }                                                        // LCOV_EXCL_STOP
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void free_size_tree()
{
  struct stat_queue * p;
  struct stat_queue * t;
  int i;

  if (tip != NULL) {
    free_node(tip);
    tip = NULL;
  }

  for (i = 0; i < QUEUE_COUNT; i++) {
    t = &queue[i];
    if (t) {
      t = t->next;
      queue[i].next = NULL;
      while (t != NULL) {
        p = t;
        t = t->next;
        free(p);
      }
    }
  }
}
