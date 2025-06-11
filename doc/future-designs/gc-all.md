Full Garbage Collection
===============================

Motivation
----------

Over time, a lot of files accumulate in the local build root. `just` has a way
to reclaim disk space while keeping the benefits of having a cache. At the same
time, one of the steps of garbage collection is generational compactification.
This is a mechanism for storing large objects in chunks, and it takes some time
to scan the youngest generation and split up the large objects.

Users may be interested in clearing the entire cache at once, so there is no
need to worry about storage validity and spend extra time on compactification.
Currently, there is no way to skip compactification, since this may invalidate
the large object CAS. This results into the need for calling `just gc` several
times manually, and the waiting time depends on the size of the youngest
generation.

Proposal
--------

The `gc` command gets an additional mode that removes all generation folders
without compactification.

Update `just` `gc` command to support the `--all` flag. This flag is
incompatible with `--no-rotate`, and an error must be reported if those two
flags are met.
