/*
  Copyright 2018 Jyri J. Virkki <jyri@virkki.com>

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
#include <string.h>

#include "dirtree.h"
#include "hashlist.h"
#include "hashlist_priv.h"
#include "main.h"
#include "paths.h"
#include "test_hashlist.h"
#include "utils.h"


static void test_hash_table_basics()
{
  char hash[hash_bufsize];

  LOG(L_PROGRESS, "--- test_hash_table_basics ---\n");
  struct hash_table * hl = init_hash_table();

  LOG(L_PROGRESS, "--- create hash table ---\n");
  assert(hl->entries > 1);
  assert(hl->table != NULL);
  for (uint16_t n = 0; n < hl->entries; n++) {
    assert(hl->table[n] == NULL);
  }
  print_hash_table(hl);

  // Add an entry for /tmp/foo and see that it is there

  LOG(L_PROGRESS, "--- add one entry---\n");
  struct direntry * tmpdir = new_child_dir("tmp", NULL);
  struct path_list_head * path_head = insert_first_path("foo", tmpdir);
  struct path_list_entry * file_entry = pb_get_first_entry(path_head);

  memset(hash, 0, hash_bufsize);
  hash[hash_bufsize-1] = 2;
  add_to_hash_table(hl, file_entry, hash);

  for (uint16_t n = 0; n < hl->entries; n++) {
    if (n != 2) { assert(hl->table[n] == NULL); }
  }

  assert(hl->table[2] != NULL);
  print_hash_table(hl);

  // Add /tmp/foo again with same hash, surely a duplicate

  LOG(L_PROGRESS, "--- add duplicate entry---\n");
  struct hash_list * hll = hl->table[2];
  add_to_hash_table(hl, file_entry, hash);
  assert(hl->table[2] == hll);
  assert(hl->table[2]->next_index == 2);
  for (uint16_t n = 0; n < hl->entries; n++) {
    if (n != 2) { assert(hl->table[n] == NULL); }
  }
  print_hash_table(hl);

  // Add files with each hash index, check all seem to be there

  LOG(L_PROGRESS, "--- add entries in every slot---\n");
  for (uint16_t n = 0; n < hl->entries; n++) {
    hash[hash_bufsize-1] = n;
    add_to_hash_table(hl, file_entry, hash);
  }

  for (uint16_t n = 0; n < hl->entries; n++) {
    if (n != 2) {
      assert(hl->table[n] != NULL);
      assert(hl->table[n]->next_index == 1);
    }
  }
  print_hash_table(hl);

  // Reset hash table, shouldn't have any valid entries

  LOG(L_PROGRESS, "--- reset hashlist---\n");
  reset_hash_table(hl);
  for (uint16_t n = 0; n < hl->entries; n++) {
    if (hl->table[n] != NULL) {
      assert(hl->table[n]->next_index == 0);
      assert(hl->table[n]->hash_valid == 0);
    }
  }
  print_hash_table(hl);

  free_hash_table(hl);
}


/** ***************************************************************************
 * Public function, see header file.
 *
 */
void test_hash_table()
{
  LOG(L_PROGRESS, "=== hash table ===\n");

  init_path_block();
  init_dirtree();
  test_hash_table_basics();
  free_dirtree();
  free_path_block();
}
