
To illustrate some ways to use dupd, let's look at exploring duplicates
in the tests directory (which I copied into /tmp/dupd for this session
just to have a shorter path).

The very first thing to do is to run a scan. The scan operation can
take various options such as -p to specify the root directory in which
to start scanning (can be repeated) and -v to show progress output (can
also be repeated for additional debug output). Run 'dupd help' for
a brief summary of all available options or 'dupd usage' for more extensive
help (or read the USAGE document in the sources, it is the same text).

For the purposes of this example, the defaults will do so I'll just
run 'dupd scan' without any additional options. This will scan all files
(except hidden files) starting from the current directory.

% pwd
/tmp/dupd

% dupd scan
Files scanned: 644
Total duplicates: 546
Run 'dupd report' to list duplicates.

The scan operation doesn't directly print all the duplicates to stdout.
Instead, it saves them in a database (SQLite) in $HOME/.dupd_sqlite by
default (the --db option can be used to override this location).

As the output says, next I can run 'dupd report' to show the list of
all duplicate sets it found. I won't show the full output to keep this
brief, but let's look at the last handful of lines which show the
duplicates using the largest amount of space:

% dupd report | tail -9

294912 total bytes used by duplicates:
  /tmp/dupd/files/file4copy2
  /tmp/dupd/files/file4copy1
  /tmp/dupd/files/file4copy3
  /tmp/dupd/files/file4


Total used: 577297 bytes (563 KiB, 0 MiB, 0 GiB)

In real world use, I'll usually look at the last hundred or so lines
of the report to find the worst offenders in terms of space usage.

Beyond that, particularly when there are millions of files, looking at
the full report is too cumbersome. This is where dupd shines, as it
supports a more interactive workflow. Let's try it:

I'll change into a directory that I'd like to work on:

% cd files2/path2
% ls
hello1

I wonder if the hello1 file is a duplicate? I could of course save the
output of 'dupd report' to a file and grep it (or use my favorite editor)
to find if /tmp/dupd/files2/path2/hello1 is listed there or not.

But there is a better way with dupd. The 'file' command reports on a
single file:

% dupd file -f hello1
DUPLICATE: /tmp/dupd/files2/path2/hello1
             DUP: /tmp/dupd/files2/path1/hello1
             DUP: /tmp/dupd/files/small1
             DUP: /tmp/dupd/files/small1copy

So now I know the file is part of a set of four duplicates and which
ones they are.

An important detail to know is that the exploratory operations (ls,
file, dupd, uniques) do more than just report on that state stored in
the database. Before showing a file to be a duplicate they will
reverify that the file still has duplicates. Let's try modifying one
of the duplicates:

% echo added >> ../path1/hello1
% dupd file -f hello1
DUPLICATE: /tmp/dupd/files2/path2/hello1
             ---: /tmp/dupd/files2/path1/hello1
             DUP: /tmp/dupd/files/small1
             DUP: /tmp/dupd/files/small1copy

The '---' in the output means that the file was a duplicate at the
time of the scan but no longer is a duplicate. What happens if we modify
the other two duplicates?

% echo added >> ../../files/small1
% echo added >> ../../files/small1copy
% dupd file -f hello1
   UNIQUE: /tmp/dupd/files2/path2/hello1
             ---: /tmp/dupd/files2/path1/hello1
             ---: /tmp/dupd/files/small1
             ---: /tmp/dupd/files/small1copy

That's right! The 'hello1' file is now unique. It had three duplicates at
the time of the scan but none are its duplicates anymore.

My usual workflow is to explore directories of interest one at a time
and as I find duplicates I decide which ones to keep and which ones to
delete. Because the 'dupd file' command reverifies duplicate or
uniqueness of the file each time, I don't accidentally end up deleting
files that no longer have duplicates!

There are limitations of course; dupd can only know about files that
existed during the scan. If new duplicate files are added or existing
files are renamed it won't know about them. However, dupd takes the
safest approach of declaring a file UNIQUE unless it is certain that
it has duplicates.

The ls, dups and uniques operations build on the file operation to
show status of multiple files in the tree insted of just one.

The 'dupd ls' operation shows the duplicate/unique status of every
file in the current directory and below:

% cd /tmp/dupd/files4
% dupd ls
DUPLICATE: /tmp/dupd/files4/z1
   UNIQUE: /tmp/dupd/files4/threediffC
DUPLICATE: /tmp/dupd/files4/three1diffC
   UNIQUE: /tmp/dupd/files4/c2
   UNIQUE: /tmp/dupd/files4/threediffA
DUPLICATE: /tmp/dupd/files4/1
DUPLICATE: /tmp/dupd/files4/three1diffB
   UNIQUE: /tmp/dupd/files4/threediffB
   UNIQUE: /tmp/dupd/files4/2
   UNIQUE: /tmp/dupd/files4/three1diffA
DUPLICATE: /tmp/dupd/files4/z2
   UNIQUE: /tmp/dupd/files4/c1
   UNIQUE: /tmp/dupd/files4/F2
   UNIQUE: /tmp/dupd/files4/F4
   UNIQUE: /tmp/dupd/files4/z
   UNIQUE: /tmp/dupd/files4/F1
DUPLICATE: /tmp/dupd/files4/3
   UNIQUE: /tmp/dupd/files4/F3

As with file, a file is only shown as DUPLICATE if it is reverified to
still have duplicates.

As you might guess, 'dupd dups' shows only the duplicates and 'dupd
uniques' shows only the unique files:

% dupd dups
/tmp/dupd/files4/z1
/tmp/dupd/files4/three1diffC
/tmp/dupd/files4/1
/tmp/dupd/files4/three1diffB
/tmp/dupd/files4/z2
/tmp/dupd/files4/3

% dupd uniques
/tmp/dupd/files4/threediffC
/tmp/dupd/files4/c2
/tmp/dupd/files4/threediffA
/tmp/dupd/files4/threediffB
/tmp/dupd/files4/2
/tmp/dupd/files4/three1diffA
/tmp/dupd/files4/c1
/tmp/dupd/files4/F2
/tmp/dupd/files4/F4
/tmp/dupd/files4/z
/tmp/dupd/files4/F1
/tmp/dupd/files4/F3

As with scan, most operations show additional info with -v:

% dupd dups -v
/tmp/dupd/files4/z1
             DUP: /tmp/dupd/files4/z2
/tmp/dupd/files4/three1diffC
             DUP: /tmp/dupd/files4/three1diffB
/tmp/dupd/files4/1
             DUP: /tmp/dupd/files4/3
/tmp/dupd/files4/three1diffB
             DUP: /tmp/dupd/files4/three1diffC
/tmp/dupd/files4/z2
             DUP: /tmp/dupd/files4/z1
/tmp/dupd/files4/3
             DUP: /tmp/dupd/files4/1

Hopefully this walkthrough gave a good feel for how to use dupd. See
'dupd help' and 'dupd usage' for additional options for some of these
operations.
