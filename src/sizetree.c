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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dbops.h"
#include "main.h"
#include "paths.h"
#include "sizetree.h"
#include "stats.h"
#include "utils.h"

struct size_node {
  long size;
  char * paths;
  struct size_node * left;
  struct size_node * right;
};

static struct size_node * tip = NULL;

struct stat_queue {
  int end;
  long size;
  char path[PATH_MAX];
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

#define PRINT_INFO  { if (thread_verbosity >= 2) { \
      printf("%scurrent_worker_queue=%s, current_producer_queue=%s\n",  \
             spaces, queue_state(current_worker_queue),                 \
             queue_state(current_worker_queue)); } }


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
 * Given that it is a new node we know it must be the first file of this
 * size being added, so store it as a new path list.
 *
 * Parameters:
 *    size - Size of this file.
 *    path - Path of this file.
 *
 * Return: ptr to the node created
 *
 */
static struct size_node * new_node(long size, char * path)
{
  struct size_node * n = (struct size_node *)malloc(sizeof(struct size_node));
  n->left = NULL;
  n->right = NULL;
  n->size = size;
  n->paths = insert_first_path(path);
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
static void add_below(struct size_node * node, long size, char * path)
{
  struct size_node * p = node;

  while (1) {
    int s = (int)size - p->size;

    if (!s) {
      insert_end_path(path, size, p->paths);
      return;
    }

    if (s > 0) {
      if (p->left == NULL) {
        p->left = new_node(size, path);
        return;
      } else {
        p = p->left;
      }
    } else {
      if (p->right == NULL) {
        p->right = new_node(size, path);
        return;
      } else {
        p = p->right;
      }
    }
  }
}


/** ***************************************************************************
 * Walk through the (presumably completed) size tree to identify size
 * nodes corresponding to only one path. Save these unique files to
 * the database.
 *
 * Parameters:
 *    dbh  - sqlite3 database handle.
 *    node - Check this node and recursively its children.
 *
 * Return: none
 *
 */
static void check_uniques(sqlite3 * dbh, struct size_node * node)
{
  int path_count = (int)*(char *)((node->paths + sizeof(char *)));

  if (path_count == 1) {
    char * path = node->paths + 3 * sizeof(char *);
    unique_to_db(dbh, path, "by-size");
  }

  if (node->left != NULL) { check_uniques(dbh, node->left); }
  if (node->right != NULL) { check_uniques(dbh, node->right); }
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
  char * spaces = "                                        [worker] ";
  struct stat_queue * worker_next;
  int want_queue = 0;

  if (thread_verbosity) {
    printf("%sthread created\n", spaces);
  }

  while (!done) {

    // Before working through a queue, need to get one first.
    pthread_mutex_lock(&queue_lock);

    while (want_queue == current_producer_queue ||
           last_owner[want_queue] == WORKER) {
      if (thread_verbosity) {
        printf("%sWant Q%d, not available, WAIT\n", spaces, want_queue);
        PRINT_INFO;
      }
      pthread_cond_signal(&queue_producer_cond);
      pthread_cond_wait(&queue_worker_cond, &queue_lock);
    }

    current_worker_queue = want_queue;

    if (last_owner[current_worker_queue] != PRODUCER) {
      printf("worker got Q%d but last_owner=%d\n",
             current_worker_queue, last_owner[current_worker_queue]);
      exit(1);
    }

    last_owner[current_worker_queue] = WORKER;

    if (thread_verbosity) {
      printf("%sTook Q%d\n", spaces, current_worker_queue);
      PRINT_INFO;
    }

    pthread_mutex_unlock(&queue_lock);

    // Work through the current queue to its end (or to the path marked end)

    if (thread_verbosity) {
      printf("%sProcessing Q%d\n", spaces, current_worker_queue);
      PRINT_INFO;
    }

    worker_next = &queue[current_worker_queue];

    while (!done && worker_next != NULL) {
      if (worker_next->end) {
        if (thread_verbosity) {
          printf("%sGot END flag: DONE\n", spaces);
          PRINT_INFO;
        }
        done = 1;

      } else {
        add_file(NULL, worker_next->size, worker_next->path);
        queue_removed[current_worker_queue]++;
        worker_next = worker_next->next;
      }
    }

    if (thread_verbosity) {
      printf("%sFinished queue %d, removed from it %ld so far\n", spaces,
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

      if (queue_added[current_worker_queue] !=
          queue_removed[current_worker_queue]) {
        printf("Q%d: added (%ld) != removed (%ld)\n",
               current_worker_queue,
               queue_added[current_worker_queue],
               queue_removed[current_worker_queue]);
        exit(1);
      }

      current_worker_queue = WANT_QUEUE;
      pthread_cond_signal(&queue_producer_cond);
      pthread_mutex_unlock(&queue_lock);
    }
  }

  if (thread_verbosity) {
    printf("%sthread finished\n", spaces);
  }

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
  free(node);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int add_file(sqlite3 * dbh, long size, char * path)
{
  (void)dbh;                    /* not used */
  if (tip == NULL) {
    tip = new_node(size, path);
    return(-2);
  }

  add_below(tip, size, path);

  return(-2);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
int add_queue(sqlite3 * dbh, long size, char * path)
{
  (void)dbh;                    /* not used */
  static char * spaces = "[scan] ";

  if (verbosity >= 6) {
    printf("%sadd_queue (%d): %s\n", spaces, current_producer_queue, path);
  }

  // Just add it to the end of the queue producer currently owns.
  producer_next->size = size;
  strcpy(producer_next->path, path);
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
  if (thread_verbosity) {
    printf("%sFinished filling Q%d\n", spaces, current_producer_queue);
    PRINT_INFO;
  }

  int want = (current_producer_queue + 1) % QUEUE_COUNT;

  pthread_mutex_lock(&queue_lock);

  current_producer_queue = WANT_QUEUE;

  while (want == current_worker_queue || last_owner[want] == PRODUCER) {
    if (thread_verbosity) {
      printf("%sWant Q%d, not available, WAIT\n", spaces, want);
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

  if (thread_verbosity) {
    printf("%sTook Q%d\n", spaces, current_producer_queue);
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
  static char * spaces = "[scan] ";

  pthread_mutex_lock(&queue_lock);

  if (thread_verbosity) {
    printf("scan_done: END in %s\n", queue_state(current_producer_queue));
    PRINT_INFO;
  }

  producer_next->end = 1;       // tells worker this will the last entry
  current_producer_queue = PRODUCER_DONE;

  pthread_cond_signal(&queue_worker_cond);
  pthread_mutex_unlock(&queue_lock);

  if (thread_verbosity) {
    printf("Waiting for sizetree worker thread to finish...\n");
  }

  pthread_join(worker_thread, NULL);

  // Verify counts for sanity checking...
  long removed = 0;
  long added = 0;
  for (int i = 0; i < QUEUE_COUNT; i++) {
    if (thread_verbosity) {
      printf("Q%d: added %ld, removed %ld\n",
             i, queue_added[i], queue_removed[i]);
    }
    removed += queue_removed[i];
    added += queue_added[i];
  }

  if (thread_verbosity) {
    printf("Total added %ld, removed %ld\n", added, removed);
  }

  if (added != removed) {
    printf("Files added in queues (%ld) != removed (%ld)\n", added, removed);
    exit(1);
  }

  if (removed != stats_files_count) {
    printf("files processed in queues (%ld) != files scanned (%ld) !!!\n",
           removed, stats_files_count);
    exit(1);
  }
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void find_unique_sizes(sqlite3 * dbh)
{
  if (tip == NULL) {
    return;
  }

  check_uniques(dbh, tip);
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
  }

  for (i = 0; i < QUEUE_COUNT; i++) {
    t = &queue[i];
    t = t->next;
    while (t != NULL) {
      p = t;
      t = t->next;
      free(p);
    }
  }
}
