SpaceFish: A Userspace File System in Shared Memory
===================================================

SpaceFish is an experimental userspace file system in shared memory,
implemented from scratch, designed to run on Linux.
It uses the `LD_PRELOAD` mechanism to intercept system call wrappers
and other filesystem-related libc functions, in order
to sandbox all file operations, in a scalable manner, at the user level.

For the duration of a spacefish session, all operations related
to files are executed on in-memory copies of the files,
with the actual filesystem left intact.
The operating-system-maintained file system is not touched,
and the OS is not even aware that its file operations
are being re-directed.

SpaceFish is specifically designed to be highly performant
on multi-core architecture. Below are some performance numbers.

Main authors
------------
Zhuangdi Xu;
Alexis Champsaur

Bucket allocation
---------------------
File data is allocated through "buckets."
Buckets can be re-used, and there are several algorithms for how free
data buckets are allocated.
Exactly one bucket allocation algorithm must
be selected through `CPPFLAGS` in the `Makefile`.

This can be:
* `ALLOC_STEPS`: Shared memory for file data is allocated from
  the OS as 1 GB steps, as needed by SpaceFish. A SpaceFish-wide "rough index"
  keeps track of approximately where the last bucket was allocated,
  and any process that needs a new buckets starts at the point
  and walks forward until it finds one. When it does, the process
  sets the rough index to the index where it founds its free bucket.
* `ALLOC_ALL_RANDOM`: "Random-and-walk" algorithm.
  All "steps" are allocated at once. That is,
  shared memory does not grow as needed by UFS. A process that needs
  a free bucket starts at a random position in the logical array
  of all data buckets, and walks forward until it finds a free one.
* `ALLOC_ALL_ROUGH_INDEX`: All steps are allocated at once.
  Then, the "rough index" mechanism described in `ALLOC_STEPS` is used
  to obtain free buckets.
* `ALLOC_ALL_PER_CORE_RANDOM`: All steps are allocated at once.
  Before looking for a free bucket, a process checks its CPU
  number and from that determines a slice of the logical array of
  all buckets from which to allocate. In this "slice", the random-and-walk
  algorithm described in `ALLOC_ALL_RANDOM` is used to obtain a free
  data bucket.
* `ALLOC_ALL_PER_CORE_ROUGH_INDEX`: Each core only has a slice, as in above case;
  however, inside its slice, the core uses the "rough index" method
  as a starting point to looking for a free bucket.

Building
--------

1. Select one bucket allocation algorithm listed above in the Makefile

2. `make`


Running
-------

1. Launch the daemon first: `./bin/daemon`

2. To run an application over SpaceFish (all file-related operations
for this application will be re-directed to SpaceFish),
set the `LD_PRELOAD` environment variable to `bin/libufs.so`.

Performance
-----------
