/*
  Copyright 2016-2021 Jyri J. Virkki <jyri@virkki.com>

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
