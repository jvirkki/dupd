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

#include "dbops.h"
#include "main.h"
#include "paths.h"
#include "sizetree.h"

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

#define STAT_QUEUE_LENGTH 500
#define STAT_QUEUE_COUNT 2
#define THREAD_IDLE -2
static struct stat_queue queue[STAT_QUEUE_COUNT];
static struct stat_queue * producer_next;
static struct stat_queue * worker_next;
static int producer_queue;
static int worker_queue;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_producer_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t queue_worker_cond = PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;


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

  if (verbosity >= 3) {
    printf("[sizetree worker] thread created\n");
  }

  // On startup we don't have a queue yet because the scanner has not added
  // any files yet which is why worker_queue starts as -1. After the scanner
  // fills up the first queue it will set worker_queue to zero and we can
  // then start working.

  pthread_mutex_lock(&queue_lock);
  while (worker_queue == -1) {
    pthread_cond_wait(&queue_worker_cond, &queue_lock);
  }
  pthread_mutex_unlock(&queue_lock);

  if (verbosity >= 3) {
    printf("[sizetree worker] thread starting\n");
  }

  while (!done) {
    if (verbosity >= 5) {
      printf("[sizetree worker] processing queue %d\n", worker_queue);
    }

    // Work through the current queue to its end (or to the path marked end)
    while (!done && worker_next != NULL) {
      if (worker_next->end) {
        if (verbosity >= 3) {
          printf("[sizetree worker] reached END flag: DONE\n");
        }
        done = 1;

      } else {
        add_file(NULL, worker_next->size, worker_next->path);
        worker_next = worker_next->next;
      }
    }

    if (verbosity >= 5) {
      printf("[sizetree worker] finished queue %d\n", worker_queue);
    }

    // Having finished this queue, worker wants the next one. However,
    // the scanner may (almost certainly) still be filling it so we need
    // to check and wait until the scanner has moved on.

    if (!done) {
      int want = (worker_queue + 1) % STAT_QUEUE_COUNT;

      pthread_mutex_lock(&queue_lock);
      worker_queue = THREAD_IDLE;
      while (want == producer_queue) {
        if (verbosity >= 5) {
          printf("[sizetree worker] want queue %d, scan still has it\n", want);
        }
        pthread_cond_wait(&queue_worker_cond, &queue_lock);
      }

      worker_queue = want;
      worker_next = &queue[worker_queue];

      pthread_cond_signal(&queue_producer_cond);
      pthread_mutex_unlock(&queue_lock);
    }
  }

  if (verbosity >= 3) {
    printf("[sizetree worker] exiting\n");
  }

  return NULL;
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

  if (verbosity >= 6) {
    printf("add_queue (%d): %s\n", producer_queue, path);
  }

  // Just add it to the end of the queue scanner currently owns.

  producer_next->size = size;
  strcpy(producer_next->path, path);
  producer_next = producer_next->next;

  // If we reached the end of this queue, need to move to the next one.

  if (producer_next == NULL) {
    pthread_mutex_lock(&queue_lock);

    // The first time around the worker has been idle waiting for the
    // first queue to be filled once. Let it know it can start.

    if (worker_queue == -1) {
      worker_queue = 0;
      worker_next = &queue[0];
    }

    // This is the queue scanner wants to start filling next but it might
    // be in use by the worker thread.

    int want = (producer_queue + 1) % STAT_QUEUE_COUNT;
    producer_queue = THREAD_IDLE;;

    while (want == worker_queue) {
      if (verbosity >= 4) {
        printf("scan wants queue %d but worker has it, waiting...\n", want);
      }
      pthread_cond_wait(&queue_producer_cond, &queue_lock);
    }

    producer_queue = want;
    producer_next = &queue[producer_queue];

    if (verbosity >= 5) {
      printf("scan switched to queue %d\n", producer_queue);
    }

    pthread_cond_signal(&queue_worker_cond);
    pthread_mutex_unlock(&queue_lock);
  }

  return(-2);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void scan_done()
{
  producer_next->end = 1;
  producer_queue = THREAD_IDLE;

  pthread_mutex_lock(&queue_lock);

  // If scanner didn't even fill one queue, worker is still waiting to start
  if (worker_queue == -1) {
    worker_queue = 0;
    worker_next = &queue[0];
  }

  pthread_cond_signal(&queue_worker_cond);
  pthread_mutex_unlock(&queue_lock);

  if (verbosity >= 4) {
    printf("waiting for sizetree worker thread to finish...\n");
  }
  pthread_join(worker_thread, NULL);
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
    for (i = 0; i < STAT_QUEUE_COUNT; i++) {
      p = &queue[i];
      for (n = 1; n < STAT_QUEUE_LENGTH; n++) {
        p->next = (struct stat_queue *)malloc(sizeof(struct stat_queue));
        p->next->end = 0;
        p = p->next;
        p->next = NULL;
      }
    }

    producer_queue = 0;
    producer_next = &queue[0];

    worker_queue = -1;
    worker_next = NULL;

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

  for (i = 0; i < STAT_QUEUE_COUNT; i++) {
    t = &queue[i];
    t = t->next;
    while (t != NULL) {
      p = t;
      t = t->next;
      free(p);
    }
  }
}
