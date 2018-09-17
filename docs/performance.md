
dupd Performance
================

The third [goal for dupd](index.md) is performance. While third on the
list of priorities, `dupd` does try to be as fast and efficient as
reasonably possible while meeting the other goals.

Performance related options
---------------------------

### Minimum Size

By default `dupd` scans all files with any content (file size 1 byte
or larger). If you know that you're only interested in larger files
(for example, if your immediate goal is to reduce disk usage) you can
exclude files smaller than some threshold from the scan with the `-m`
or `--minsize` option.


Performance Comparisons
-----------------------

Performance comparisons go out of date fairly quickly, both because
`dupd` changes and because other duplicate finders change. Also, any
benchmark results are by nature specific to the data set used and the
hardware and OS and filesystem configuration. For these reasons this
documentation doesn't include specific numbers.

However, I do occasionaly run tests and comparisons on various data
sets and publish the results on my blog, which might be of interest.

* [Duplicate finder performance comparison (2018)](http://www.virkki.com/jyri/articles/index.php/duplicate-finder-performance-2018-edition/)
* [dupd 1.4 --hdd mode (2017)](http://www.virkki.com/jyri/articles/index.php/dupd-introducing-hdd-mode/)
* [dupd 1.3 vs. jdupes (take two) (2016)](http://www.virkki.com/jyri/articles/index.php/dupd-vs-jdupes-take-2/)
* [dupd 1.3 vs. jdupes (2016)](http://www.virkki.com/jyri/articles/index.php/dupd-vs-jdupes/)
* [Performance improvements in dupd 1.2 (2015)](http://www.virkki.com/jyri/articles/index.php/some-dupd-performance-improvements/)
* [dupd 1.1 benchmarking (2015)](http://www.virkki.com/jyri/articles/index.php/duplicate-file-detection-performance/)
* [dupd 1.0 benchmarking (2012)](http://www.virkki.com/jyri/articles/index.php/duplicate-file-detection-with-dupd/)

---
[Back to the dupd documentation index](index.md)

