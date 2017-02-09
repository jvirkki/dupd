
dupd Performance
================

The third [goal for dupd](index.md) is performance. While third on the
list of priorities, `dupd` does try to be as fast and efficient as
reasonably possible while meeting the other goals.

Performance related options
---------------------------

### SSD or HDD mode

Before doing a scan the most important performance-related
consideration is whether the files to be scanned are on an SSD (solid
state drive) or a HDD (hard disk drive, aka spinning rust).

If the file content needs to be read from a HDD, the I/O wait times
(for the heads to move and the disk to spin to the right place) are by
far the most time consuming (in terms of wall clock time) part of the
scan. On the other hand when reading from an SSD the file access times
are not only faster but also very even.

For this reason `dupd` implements two different reading algorithms for
the `scan` command. The SSD mode is lighter in CPU and memory usage
and faster on SSD media. The HDD mode has more overhead and memory
usage but it is faster (usually far faster) on HDDs.

**The SSD mode is the default scan mode.**

This means that when scanning files from a HDD, be sure to give the `--hdd`
option to enable this mode for best performance (but, see below).

```
% dupd scan --hdd
```

### Minimum Size

By default `dupd` scans all files with any content (file size 1 byte
or larger). If you know that you're only interested in larger files
(for example, if your immediate goal is to reduce disk usage) you can
exclude files smaller than some threshold from the scan with the `-m`
or `--minsize` option.


File cache considerations
-------------------------

The `--hdd` mode naming makes a simplifying assumption. The reality is
that it doesn't matter whether the file content lives on a HDD. What
matters is whether the file content will be *read* from a HDD. Subtle
but important difference.

If the files being scanned are in the file cache (in RAM) then access
patterns are closer to the SSD mode (but faster), even if the
underlying data lives on a HDD. What this means is that the (default)
SSD mode may be faster even if your machine has a HDD.

But, are (most of) the files in the cache?

If you are using `dupd` in "[the dupd way](examples.md)" it means
you'll be repeatedly running `dupd scan` as you explore directories
with the interactive commands. This means the file content is highly
likely to be in the cache during the second and subsequent `scan`
operations.

Here is an example from one system I have where the data is on a HDD:

* First scan in SSD mode: 13 minutes
* First scan in HDD mode: 1 minute
* Second scan in SSD mode: 2 seconds
* Second scan in HDD mode: 4 seconds

As you can see, the HDD mode is vastly faster when reading from the
physical disk, but twice as slow when reading from the populated file
cache.

As a rule of thumb on a HDD I recommend:

1. Always run the first scan with `dupd scan --hdd`
2. Work interactively as desired
3. Run the second scan in SSD mode with `dupd scan`
   * If this is slower, always use `--hdd` mode for that data set
   * If it is faster, use SSD mode during subsequent scans in a work session

As with all things performance, YMMV. See what works best on your
system given the size of your data set, the RAM available and the
other activities going on in that machine.

Performance Comparisons
-----------------------

Performance comparisons go out of date fairly quickly, both because
`dupd` changes and because other duplicate finders change. Also, any
benchmark results are by nature specific to the data set used and the
hardware and OS and filesystem configuration. For these reasons this
documentation doesn't include specific numbers.

However, I do occasionaly run tests and comparisons on various data
sets and publish the results on my blog, which might be of interest.

* [dupd 1.3 vs. jdupes (take two) (2016)](http://www.virkki.com/jyri/articles/index.php/dupd-vs-jdupes-take-2/)
* [dupd 1.3 vs. jdupes (2016)](http://www.virkki.com/jyri/articles/index.php/dupd-vs-jdupes/)
* [Performance improvements in dupd 1.2 (2015)](http://www.virkki.com/jyri/articles/index.php/some-dupd-performance-improvements/)
* [dupd 1.1 benchmarking (2015)](http://www.virkki.com/jyri/articles/index.php/duplicate-file-detection-performance/)
* [dupd 1.0 benchmarking (2012)](http://www.virkki.com/jyri/articles/index.php/duplicate-file-detection-with-dupd/)

---
[Back to the dupd documentation index](index.md)

