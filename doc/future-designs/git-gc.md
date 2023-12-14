# Garbage Collection for Repository Roots

## Current status and shortcomings

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

## Proposed changes

We propose to add generational garbage collection for the repository
roots, similar to the one we have for the main CAS, action cache,
and target-level cache.

### Change of layout in the local build root

For the repository roots, there are consistency conditions in the local
build root: an entry in any of the map directories (`tree-map/archive`,
`tree-map/zip`, `distfile-tree-map`, etc) promises that the referenced
tree is available in the local `git` repository.

So, in order to allow atomic generation rotation, and hence to
reliably preserve the internal consistency, the `git` repository
and the map directories are put into generation directories. More
precisely, for the youngest generation, the respective directories
reside in `roots/generation-0` and for the older generation in
`roots/generation-1`.

### Use of old generations when setting up roots

When `just-mr` looks into one of the generation-controlled resources,
it does so in the youngest generation. If found there, it will
proceed as done currently. If not found, it will immediately look
into the corresponding resource in the older generation; if available
in the older generation it will promote the found object in the
following way.

 - To promote a `git` commit, a fetch from the old-generation
   repository to the new-generation repository will be carried
   out (using `libgit`'s functionality). As a fetch from a repository
   on the same file system is backed by hard links, no significant
   storage overhead will occur. The promoted commit will be tagged
   in the new-generation repository to ensure it stays there
   persistently. As usual, the tag name is encoding the commit id,
   so that no conflicts occcur.

 - To promote a `git` tree, a commit is created with this tree as
   tree, a commit message that is a function of the tree id, and
   no parents. That commit is promoted.

 - To promote an entry in one of the maps, first the corresponding
   tree will be promoted, then the entry itself will be promoted
   by creating a hard link.

### New command `just-mr gc-repo`

The multi-repository tool `just-mr` will get a new subcommand `gc-repo`,
a name chosen to not conflict with the laucher functionality; recall
that `just-mr gc` will simply call `just gc`. This new `just-rm
gc-repo` command rotates the generations: the old generation will
be removed and the new one will become (atomically by a rename)
the old one.

### Locking

To avoid interference between setting up the various repository
roots needed for one multi-repository build and the repository
garbage collection, we use an `flock`-based locking, similar as we
do for the main CAS. There is a repository-gc lock.

 - Any invocation of `just-mr` apart from `just-mr gc-repo` takes a
   shared lock and keeps it over its whole lifetime. When invoked as
   a launcher, the lock is kept over the `exec` so that the launched
   process can rely on the roots not being garbage collected.

 - An invocation of `just-mr gc-repo` takes an exclusive lock for
   the period it does the directory renames.

## Considerations on the transition

As there is no overlap between the old and the new locations of
the root-related directories, correctness is not affected by this
transition. However, the `git` repository and the map directories
in the old location will become unused and therefore pointlessly
waste disk space. The upgrade notes in our changelog will therefore
recommend to either manually create the generation directories
and move the `git` repository and the map directories there, or to
alternatively remove them.
