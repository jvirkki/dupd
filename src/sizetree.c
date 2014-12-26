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

#include <stdio.h>
#include <stdlib.h>

#include "dbops.h"
#include "paths.h"
#include "sizetree.h"

struct size_node {
  long size;
  char * paths;
  struct size_node * left;
  struct size_node * right;
};

static struct size_node * tip = NULL;


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
 * Add the give size and path below the given node (recursively as needed).
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
  if (size < node->size) {
    if (node->left != NULL) {
      add_below(node->left, size, path);
    } else {
      node->left = new_node(size, path);
    }
    return;
  }

  if (size > node->size) {
    if (node->right != NULL) {
      add_below(node->right, size, path);
    } else {
      node->right = new_node(size, path);
    }
    return;
  }

  // If we got here means we have this size already so append the current
  // path to the end of the existing path list.

  insert_end_path(path, size, node->paths);
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
    char * path = node->paths + 2 * sizeof(char *);
    unique_to_db(dbh, path, "by-size");
  }

  if (node->left != NULL) { check_uniques(dbh, node->left); }
  if (node->right != NULL) { check_uniques(dbh, node->right); }
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
  (void)dbh;			/* not used */
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
void free_size_tree()
{
  if (tip == NULL) {
    return;
  }

  free_node(tip);
}
