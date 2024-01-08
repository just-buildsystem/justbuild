# Handling of large blobs

## Current situation and shortcomings

When building locally, all intermediate artifacts end up in the
local CAS. Depending on the nature of the artifacts built, this
can include large ones (like disk images) that only differ in small
parts from the ones created in previous builds for a code base
with only small changes. In this way, the disk can fill up quite
quickly. Of course, space can always be reclaimed by `just gc`,
but that would limit how far the cache reaches back.

## Proposed changes

We propose to use the same solution that is already used for reducing
traffic when downloading large final artifacts for installing; there,
large blobs are split in a content-defined way and only chunks not
already known have to be transferred. Similarly, when storing the
chunks the large blobs are composed of, duplicate blobs have to be
stored only once.

### New store: large-objects CAS

A new form of CAS is added, called large-objects CAS. It follows
the same generation regime as the regular CAS; more precisely,
next to the already existing `casf`, `casx`, and `cast` two new
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
- the object itself is spliced on disk in a temproary file,
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

Additionally, the `gc` will get an option `--compactify-only`
instructing `gc` to only perform the compactification step, without
rotating generations.
