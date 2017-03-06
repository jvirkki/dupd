dupd
====

dupd is a file duplicate detection CLI utility.

Goals
-----

dupd is designed to meet the following goals in order of decreasing importance:

1. Safe

   dupd does not delete files.

2. Convenient for humans

   This goal was my motivation for developing dupd. It is also the
   primary differentiator for dupd.

   All duplicate finders will output a list of duplicates in some form,
   but then what?

   If the number of duplicate files is quite small that works nicely.
   But if there are hundreds of thousands of duplicates, there is no way
   I could possibly process that output. My initial use case for dupd
   was for a system with about a million files and roughly 250K duplicates.
   I needed a tool to not just find the duplicates but, much more importantly,
   help me manage the process of identifying what to keep in an orderly
   fashion in small chunks. The result is dupd.
   See [Finding duplicates the dupd way](examples.md) for examples.

3. Fast

   Fast is fun.


Documentation Links
-------------------

* [README](../README)
* [BUILDING](../BUILDING)
* [Manual page](../man/dupd)
* [Finding duplicates the dupd way](examples.md)
* [Performance](performance.md)
* [Design Choices](design.md)
* [Contribution guidelines](contributing.md)

