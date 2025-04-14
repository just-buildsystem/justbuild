Symbolic links
==============

Background
----------

Besides files and directories, symbolic links are also an important
entity in the file system. Also `git` natively supports symbolic links
as entries in a tree object. Technically, a symbolic link is a string
that can be read via `readlink(2)`. However, they can also be followed
and functions to access a file, like `open(2)` do so by default. When
following a symbolic link, both, relative and absolute, names can be
used.

Symbolic links in build systems
-------------------------------

### Follow and reading both happen

Compilers usually follow symlinks for all inputs. Archivers (like
`tar(1)` and package-building tools) usually read the link in order to
package the link itself, rather than the file referred to (if any). As a
generic build system, it is desirable to not have to make assumptions on
the intention of the program called (and hence the way it deals with
symlinks). This, however, has the consequence that only symbolic links
themselves can properly model symbolic links.

### Self-containedness and location-independence of roots

From a build-system perspective, a root should be self-contained; in
fact, the target-level caching assumes that the git tree identifier
entirely describes a `git`-tree root. For this to be true, such a root
has to be both, self contained and independent of its (assumed) location
in the file system. In particular, we can neither allow absolute
symbolic links (as they, depending on the assumed location, might point
out of the root), nor relative symbolic links that go upwards (via a
`../` reference) too far.

### Symbolic links in actions

Like for source roots, we understand action directories as self
contained and independent of their location in the file system.
Therefore, we have to require the same restrictions there as well, i.e.,
neither absolute symbolic links nor relative symbolic links going up too
far.

Allowing all relative symbolic links that don't point outside the
action directory, however, poses an additional layer of complications in
the definition of actions: a string might be allowed as symlink in some
places in the action directory, but not in others; in particular, we
can't tell only from the information that an artifact is a relative
symlink whether it can be safely placed at a particular location in an
action or not. Similarly for trees for which we only know that they
might contain relative symbolic links.

### Presence of symbolic links in system source trees

It can be desirable to use system libraries or tools as dependencies. A
typical use case, but not the only one, is packaging a tool for a
distribution. An obvious approach is to declare a system directory as a
root of a repository (providing the needed target files in a separate
root). As it turns out, however, those system directories do contain
symbolic links, e.g., shared libraries pointing to the specific version
(like `libfoo.so.3` as a symlink pointing to `libfoo.so.3.1.4`) or
detours through `/etc/alternatives`.

Bootstrapping "shopping list" option
------------------------------------

In order to more easily support building the tool itself against
pre-installed dependencies with the respective directories containing
symbolic links, or tools (like `protoc`) being symbolic links (e.g., to
the specific version), repositories can specify, in the `"copy"`
attribute of the `"local_bootstrap"` parameter, a list of files and
directories to be copied as part of the bootstrapping process to a fresh
clean directory serving as root; during this copying, symlinks are
followed.

Our treatment of symbolic links
-------------------------------

### "Ignore-special" roots

For cases where we simply have no need for special entries, all the existing
roots have "ignore-special" versions thereof. In such a root
(regardless whether file based, or `git`-tree based), everything
not a file or a directory is pretended to be absent. For any
compile-like tasks, the effect of symlinks can be modeled by appropriate
staging.

As certain entries have to be ignored, source trees can only be obtained
by traversing the respective tree; in particular, the `TREE` reference
is no longer constant time on those roots, even if `git`-tree based.
Nevertheless, for `git`-tree roots, the effective tree is a function of
the `git`-tree of the root, so `git`-tree-based ignore-special roots are
content fixed and hence eligible for target-level caching.

### Non-upwards relative symlinks as first-class objects

A restricted form of symlinks, more precisely *relative*
*non-upwards symbolic links*, exist as first-class objects.
That is, a new artifact type (besides blobs and trees) for relative
non-upwards symbolic links has been introduced. Like any other artifact,
they can be freely placed into the inputs of an action, as well as in artifacts,
runfiles, or provides map of a target. Artifacts of this new type can be
defined as:

 - source-symlink reference, as well as implicitly as part of a source
   tree,
 - as a symlink output of an action, as well as implicitly as part of a
   tree output of an action, and
 - explicitly in the rule language from a string through a new
   `SYMLINK` constructor function.

While existing as separate artifacts in order to properly stage them, (relative
non-upwards) symbolic links are, in many aspects, simple files with elevated
permissions. As such, they locally use the existing file CAS. Remotely, the
existing execution protocol already allows the handling of symbolic links via
corresponding Protobuf messages, therefore no extensions are needed.

Additionally, the built-in rules are extended with a `"symlink"` target,
allowing the generation of a symlink with given non-upwards target path.

### Import resolved `git`-trees

Finally, to be as flexible as possible in handling external repositories with
(possibly) upwards symbolic links, we allow filesystem directories and archives
to be imported also as partially or completely resolved `git`-trees.

In a *partially resolved tree*, all relative upwards symbolic links confined to
the tree get resolved, i.e., replaced by a copy of the entry they point to, if
existing, or removed otherwise. This of course leaves relative non-upwards
symbolic links in the `git`-tree, as they are supported objects.

Alternatively, in a *completely resolved tree*, all relative symbolic links
confined to the tree (whether upwards or not) get resolved, resulting in a
`git`-tree free of all symbolic links.

For reasons already described, absolute symbolic links are never supported.

As this process acts directly at the repository level, the resulting roots
remain cacheable and their trees accessible in constant time. Moreover, to
increase the chances of cache hits in `just-mr`, not only the resulting
resolved trees are stored, but also the original, unresolved ones.
