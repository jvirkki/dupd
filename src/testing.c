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
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "dirtree.h"
#include "main.h"
#include "test_hashlist.h"
#include "testing.h"
#include "utils.h"


static void test_dirtree()
{
  char buffer[1024];
  LOG(L_PROGRESS, "=== dirtree ===\n");

  init_path_block();
  init_dirtree();
  struct direntry * root = new_child_dir("/", NULL);
  build_path_from_string("something", root, buffer);
  assert(!strcmp("/something", buffer));
  free_dirtree();
}


void testing()
{
  test_hash_table();
  test_dirtree();
}
