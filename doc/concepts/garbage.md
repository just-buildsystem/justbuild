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

Compactification as part of garbage collection
----------------------------------------------

When building locally, all intermediate artifacts end up in the
local CAS. Depending on the nature of the artifacts built, this
can include large ones (like disk images) that only differ in small
parts from the ones created in previous builds for a code base
with only small changes. In this way, the disk can fill up quite
quickly. Reclaiming this disk space by the described generation
rotation would limit how far the cache reaches back. Therefore,
large blobs are split in a content-defined way; when storing the
chunks the large blobs are composed of, duplicate blobs have to be
stored only once.

### Large-objects CAS

Large objects are stored in a separated from of CAS,
called large-objects CAS. It follows
the same generation regime as the regular CAS; more precisely,
next to the `casf`, `casx`, and `cast` two additional
entries are generated, `cas-large-f` and `cas-large-t`, where the
latter is only available in native mode (i.e., when trees are hashed
differently than blobs).

The entries in the large-objects CAS are keyed by hash of the
large object and the value of an entry is the concatenation of the
hashes of the chunks the large object is composed of. An entry in
a large-object CAS promises

- that the chunks the large object is composed of are in the main
  CAS, more precisely `casf` in the same generation,
- the concatenation of the specified chunks indeed gives the
  requested object,
- if the object is a tree, the parts are also in the same generation,
  in main or larger-object CAS, and
- the object is strictly larger than the maximal size a chunk
  obtained by splitting can have.

Here, the last promise avoids that chunks of a large object later
can be replaced by a large-object entry themselves.

### Using objects from the large-objects CAS

Whenever an object is not found in the main CAS, the large-objects
CAS is inspected. If found there, then, in this order,

- if the entry is not already in the youngest generation, the chunks
  are promoted to the youngest generation,
- the object itself is spliced on disk in a temporary file,
- if the object is a tree, the parts are promoted to the youngest
  generation (only necessary if the large-object entry was not
  found in the youngest generation anyway),
- the large object is added to the respective main CAS, and finally
- the large-objects entry is added to the youngest generation (if
  not already there).

The promoting of the chunks ensures that they are already present
in the youngest generation at almost no storage cost, as promoting
is implemented using hard links. Therefore, the overhead of a later
splitting of that blob is minimal.

### Blob splitting uses large-objects CAS as cache

When `just execute` is asked to split an object, first the
large-objects CAS is inspected. If the entry is present in the
youngest generation, the answer is directly served from there;
if found in an older generation, it is served from there after
appropriate promotion (chunks; parts of the tree, if the object to
split was a tree; large-objects entry) to the youngest generation.

When `just execute` actually performs a split and the object that
was to be splitted was larger than the maximal size of a chunk,
after having added the chunks to the main CAS, it will also write
a corresponding entry to the large-objects CAS. In this way,
subsequent requests for the same object can be served from there
without having to inspect the object again.

Similarly, if a blob is split in order to be transferred to an
endpoint that supports blob-splicing, a corresponding entry is
added to the large-objects CAS (provided the original blob was
larger than the maximal size of a chunk, but we will transfer by
blob splice only for those objects anyway).

### Compactification of a CAS generation

During garbage collection, while already holding the exclusive
garbage-collection lock, the following compactification steps are
performed on the youngest generation before doing the generation
rotation.

- For every entry in the large-objects CAS the corresponding entries
  in the main CAS are removed (for an entry in `cas-large-f` the
  entries in both, `casf` and `casx` are removed, as files and
  executable files are both hashed the same way, as blobs).
- For every entry in the main CAS that is larger than the
  compactification threshold, the object is split, the chunks are
  added to the main CAS, the list of chunks the object is composed
  of is added to the large-objects CAS, and finally the object is
  removed from the main CAS.

It should be noted that these steps do not modify the objects
that can be obtained from that CAS generation. In particular, all
invariants are kept.

As compactification threshold we chose a fixed value larger than
the maximal size a chunk obtained from splitting can have. More
precisely, we take the maximal value where we still can transfer
a blob via `grpc` without having to use the streaming interface,
i.e, we chose 2MB. In this way, we already have correct blobs split
for transfer to an end point that supports blob splicing.

The compactification step will also be carried out if the `--no-rotate`
option is given to `gc`.

The compactification step is skipped if the `--all` option is given to
`gc`, since that option triggers removal of all cache generations.

`--no-rotate` and `--all` are incompatible options.

Garbage Collection for Repository Roots
---------------------------------------

The multi-repository tool `just-mr` often has to create roots: the
tree for an archive, an explicit `"git tree"` root, etc. All those
roots are stored in a `git` repository in the local build root.
They are fixed by a tagged commit to be persistent there. In this
way, the roots are available long-term and the association between
archive hash and resulting root can be persisted. The actual archive
can eventually be garbage collected as the root can efficiently be
provided by that cached association.

While this setup is good at preserving roots in a quite compact
form, there currently is no mechanism to get rid of roots that are
no longer needed. Especially switching between projects that have
a large number of third-party dependencies, or on projects changing
their set of dependencies frequently, this `git` repository in the
local build root can grow large.

Therefore, the repository roots follow a similar generation regime.
The subcommand `gc-repo` of `just-mr` rotates generations and removes
the oldest one. Whenever an entry is not found in the youngest
generation of the repository-root storage, older generations are
inspected first before calling out to the network; entries found
in older generations are promoted to the youngest.
