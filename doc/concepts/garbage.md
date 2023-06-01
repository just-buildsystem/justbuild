Garbage Collection
==================

For every build, for all non-failed actions an entry is created in the
action cache and the corresponding artifacts are stored in the CAS. So,
over time, a lot of files accumulate in the local build root. Hence we
have a way to reclaim disk space while keeping the benefits of having a
cache. This operation is referred to as garbage collection and usually
uses the heuristics to keeping what is most recently used. Our approach
follows this paradigm as well.

Invariants assumed by our build system
--------------------------------------

Our tool assumes several invariants on the local build root, that we
need to maintain during garbage collection. Those are the following.

 - If an artifact is referenced in any cache entry (action cache,
   target-level cache), then the corresponding artifact is in CAS.
 - If a tree is in CAS, then so are its immediate parts (and hence also
   all transitive parts).

Generations of cache and CAS
----------------------------

In order to allow garbage collection while keeping the desired
invariants, we keep several (currently two) generations of cache and
CAS. Each generation in itself has to fulfill the invariants. The
effective cache or CAS is the union of the caches or CASes of all
generations, respectively. Obviously, then the effective cache and CAS
fulfill the invariants as well.

The actual `gc` command rotates the generations: the oldest generation
is be removed and the remaining generations are moved one number up
(i.e., currently the young generation will simply become the old
generation), implicitly creating a new, empty, youngest generation. As
an empty generation fulfills the required invariants, this operation
preservers the requirement that each generation individually fulfill the
invariants.

All additions are made to the youngest generation; in order to keep the
invariant, relevant entries only present in an older generation are also
added to the youngest generation first. Moreover, whenever an entry is
referenced in any way (cache hit, request for an entry to be in CAS) and
is only present in an older generation, it is also added to the younger
generation, again adding referenced parts first. As a consequence, the
youngest generation contains everything directly or indirectly
referenced since the last garbage collection; in particular, everything
referenced since the last garbage collection will remain in the
effective cache or CAS upon the next garbage collection.

These generations are stored as separate directories inside the local
build root. As the local build root is, starting from an empty
directory, entirely managed by \`just\` and compatible tools,
generations are on the same file system. Therefore the adding of old
entries to the youngest generation can be implemented in an efficient
way by using hard links.

The moving up of generations can happen atomically by renaming the
respective directory. Also, the oldest generation can be removed
logically by renaming a directory to a name that is not searched for
when looking for existing generations. The actual recursive removal from
the file system can then happen in a separate step without any
requirements on order.

Parallel operations in the presence of garbage collection
---------------------------------------------------------

The addition to cache and CAS can continue to happen in parallel; that
certain values are taken from an older generation instead of freshly
computed does not make a difference for the youngest generation (which
is the only generation modified). But build processes assume they don't
violate the invariant if they first add files to CAS and later a tree or
cache entry referencing them. This, however, only holds true if no
generation rotation happens in between. To avoid those kind of races, we
make processes coordinate over a single lock for each build root.

 - Any build process keeps a shared lock for the entirety of the build.
 - The garbage collection process takes an exclusive lock for the
   period it does the directory renames.

We consider it acceptable that, in theory, local build processes could
starve local garbage collection. Moreover, it should be noted that the
actual removal of no-longer-needed files from the file system happens
without any lock being held. Hence the disturbance of builds caused by
garbage collection is small.
