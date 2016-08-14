/*
  Copyright 2016 Jyri J. Virkki <jyri@virkki.com>

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

/*
  Undocumented options / behavior
  ===============================
  Adding this section to "document" the undocumented.

  WARNING: Anything here should be considered a private interface
  which might go away on a whim at any time. That's why they are
  undocumented. If you find that you NEED any of this, let me know the
  use case so I can make it more reliable.


  Configurable path separator (probably to be removed in 2.0)
  -----------------------------------------------------------
  The scan operation takes option --pathsep:

  --pathsep CHAR     Change the internal path separator character to CHAR.
                     When a list of paths is saved to the database, they are
                     separated by this character. The result is that any files
                     which contains this character in the name will be ignored
                     during scan because the name conflicts with the separator.

  The default path separator is 0x1C (decimal 28) which is the rarely
  used ASCII file separator character.


  Testing small buffers
  ----------------------
  Global option x-small-buffers reduces some memory allocation defaults.
  The only use for this is to force reallocations earlier to test those
  code paths with smaller data sets. Unless you are writing test cases,
  never use this.


  Testing
  -------
  Global option x-testing triggers test-only internal behavior changes.
  Unless you are writing test cases, never use this. Probably not even then.


 */
