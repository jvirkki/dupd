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

#ifndef _DUPD_REPORT_H
#define _DUPD_REPORT_H

#include <sqlite3.h>
#include <sys/types.h>


/** ***************************************************************************
 * Prints a report on the duplicates to stdout.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void operation_report();


/** ***************************************************************************
 * Creates a shell script which can delete all the duplicates.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void operation_shell_script();


/** ***************************************************************************
 * Print status of a single file.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void operation_file();


/** ***************************************************************************
 * List all the known-unique files within a given path.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void operation_uniques();


/** ***************************************************************************
 * List all the known-duplicatefiles within a given path.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void operation_dups();


/** ***************************************************************************
 * List info about all files within a given path.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void operation_ls();


/** ***************************************************************************
 * Validate that all duplicate sets in the database are still duplicates
 * by hashing each file (potentially slow, for many duplicates).
 *
 * Parameters: none
 *
 * Return: 1 if non-duplicates were found.
 *
 */
int operation_validate();


/** ***************************************************************************
 * Operation 'hash', hash a single file and display result.
 * If applicable, saves hash in cache.
 *
 * Parameters: none
 *
 * Return: none
 *
 */
void operation_hash_file();


#endif
