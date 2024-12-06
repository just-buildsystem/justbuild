Computed roots
==============

Status quo
----------

As of version `1.0.0`, the `just` build tool requires the repository
configuration, including all roots, to be specified ahead of time. This
has a couple of consequences.

### Flexible source views, thanks to staging

For source files, the flexibility of using them in a layout different
from how they occur in the source tree is gained through staging. If a
different view of sources is needed, instead of a source target, a
defined target can be used that rearranges the sources as desired. In
this way, also programmatic transformations of source files can be
carried out (while the result is still visible at the original
location), as is done, e.g., by the `["patch", "file"]` rule of the
`just` main repository.

### Restricted flexibility in target-definitions via globbing

When defining targets, the general principle is that the definition of
target and action graph only depends on the description (given by the
target files, the rules and expressions, and the configuration). There
is, however, a single exception to that rule: a target file may use the
`GLOB` built-in construct and in this way depend on the index of the
respective source directory. This allows, e.g., to define a separate
action for every source file and, in this way, get good incrementality
and parallelism, while still having a concise target description.

### Modularity in rules through expressions

Rules might share common tasks. For example, for both `C` binaries and
`C` libraries, the source files have to be compiled to object files. To
avoid duplication of descriptions, expressions can be called (also from
expressions themselves).

Use cases that require more flexibility
---------------------------------------

### Generated target files

Sometimes projects (or parts thereof that can form a separate logical
repository) have a simple structure. For example, there is a list of
directories and for each one there is a library, named and staged in a
systematic way. Repeating all those systematic target files seems
unnecessary work. Instead, we could store the list of directories to
consider and a small script containing the naming/staging/globbing
logic; this approach would also be more maintainable. A similar approach
could also be attractive for a directory tree with tests where, on top,
all the individual tests should be collected to test suites.

### Staging according to embedded information

For importing prebuilt libraries, it is sometimes desirable to stage
them in a way honoring the embedded `soname`. The current approach is to
provide that information out of band in the target file, so that it can
be used during analysis. Still, the information is already present in
the prebuilt binary, causing unnecessary maintenance overhead; instead,
the target file could be a function of that library which can form its
own content-fixed root (e.g., a `git tree` root), so that the computed
value is easily cacheable.

### Simplified rule definition and alternative syntax

Rules can share computation through expressions. However, the interface,
deliberately has to be explicit, including the documentation strings
that are used by `just describe`. While this allows easy and efficient
implementation of `just describe`, there is some redundancy involved, as
often fields are only there to be used by a common expression, but this
have to be documented in a redundant way (causing additional maintenance
burden).

Moreover, using JSON encoding of abstract syntax trees is an
unambiguously readable and easy to automatically process format, but
people argue that it is hard to write by hand. However, it is unlikely
to get agreement on which syntax is best to use. Now, if rule and
expression files could be generated, this argument would not be
necessary. Moreover, rules are typically versioned and infrequently
changed, so the step of generating the official syntax from the
convenient one would typically be in cache.

Proposal: Support computed roots
--------------------------------

We propose computed roots as a clean principle to add the needed (and a
lot more) flexibility for the described use cases, while ensuring that
all computations of roots are properly cacheable at high level. In this
way, we do not compromise efficient builds, as the price of the
additional flexibility, in the typical case, is just a single cache
lookup. Of course, it is up to the user to ensure that this case really
is the typical one, in the same way as it is their responsibility to
describe the targets in a way to have proper incrementality.

### New root type `"computed"`

The `just` multi-repository configuration will allow a new type of root
(besides `"file"` and `"git tree"` and variants thereof), called
`"computed"`. A `"computed"` root is given by

 - the (global) name of a repository
 - the name of a target (in `["module", "target"]` format), and
 - a configuration (as JSON object, taken literally).

It is a requirement that the specified target is an `"export"` target
and the specified repository content-fixed; `"computed"` roots are
considered content-fixed. However, the dependency structure of computed
roots must be cycle free. In other words, there must exist an ordering
of computed roots (the implicit topological order, not a declared one)
such that for each computed root, the referenced repository as well as
all repositories reachable from that one via the `"bindings"` map only
contain computed roots earlier in that order.

### New root type `"tree structure"`

In the described use case of generated target files, the tree of
target files only depends on the structure of the workspace root. To
avoid unnecessary actions, an additional new root type is defined,
that of a `"tree structure"`. Such a root is given by precisely
one root. It evaluates to that root but with all files replaced
by empty files. Obviously, this computation can be done without
spawning actions and is cachable.

The serve functionality is extended to also answer queries for the
tree structure of a given tree.

### Strict evaluation of roots as artifact tree

The building of required computed roots happens in topological order;
the build of the defining target of a root is, in principle (subject to
a user-defined restriction of parallelism) started as soon as all roots
in the repositories reachable via bindings are available. The root is
then considered the artifact tree of the defining target.

In particular, the evaluation is strict: all roots of reachable
repositories have to be successfully computed before the evaluation is
started, even if it later turns out that one of these roots is never
accessed in the computation of the defining target. The reason for this
strictness requirement is to ensure that the cache key for target-level
caching can be computed ahead of time (and we expect the entry to be in
target-level cache most of the time anyway).

### Intensional equality of computed roots

During a build, each computed root is evaluated only once, even if
required in several places. Two computed roots are considered equal, if
they are defined in the same way, i.e., repository name, target, and
configuration agree. The repository or layer using the computed root is
not part of the root definition. Similarly, two tree-structure roots
are equal if the defining roots are equal.

### Evaluation through serve endpoint preferred

When determining the value of a computed root, as for every export
target, the provided serve endpoint (if any) is consulted first.
Only if it is not aware of the root, a local evaluation is carried
out. This strategy is also applied for tree-stucture roots.

### Computed roots available to the user

As computed roots are defined by export targets, the respective
artifacts are stored in the local CAS anyway. Additionally, the tree
that forms the root will be added to CAS as well. Moreover, an option
will be added to specify a log file that contains, in machine-readable
way, all the tree identifiers of all computed roots used in this build,
together with their definition.

### `just-mr` to support computed roots

To allow simply setting up a `just` configuration using computed roots,
`just-mr` will allow a repository type `"computed"` with the same
parameters as a computed root. These repositories can be used as roots,
like any other `just-mr` repository type. When generating the `just`
multi-repository configuration, the definition of a `"computed"`
repository is just forwarded as computed root.

### Computed roots and `just serve`

Due to the presence of `just serve`, roots can be absent. This
affects computed roots in two ways,
 - roots, in particular the target roots, of the repository referred
   to can be absent, and
 - a computed root can be absent itself.
The latter has to be supported, as dependencies that should be
delegated to `just serve` might contain computed roots themselves.
In this case, we consider it acceptable to have one round of talking
back and forth with the serve instance per computed root involved,
however we do not want to fetch the artifacts of those intermediate
roots. After all, whole point of the serve service was to use
dependencies without having them locally.

#### Sytnax for absent computed roots

As for other roots, we let the user specify which roots are to be
absent. Tools like `just-import-git` will extend their marking of absent
dependencies (e.g., by the option `--absent` of `just-import-git`)
to computed roots as well.

In a `just-mr` repository config, `"pragma": {"absent": true}` can
be used for computed roots as well. Also `just-mr` will also honor
the passed absent specification (via `--absent` or implicitly via
the rc file) for computed roots the same way as for other roots.

In a `just` repository config, computed roots are given by the
tuple `["computed", <repository>, <module>, <target>, <config>]`.
Optionally, an additional entry can be added; that entry has to be
an object. A computed root is absent if that additional argument
is present and contains an entry for the value `"absent"` that
is `true`. E.g., `["computed", "base", "", "", {}]` is a concrete
computed root and `["computed", "base", "", "", {}, {"absent":
true}]` is the same computed root considered absent.

### Evaluation of computed roots in connection with absent roots

If a computed root is absent then, regardless of whether the base
repository is absent or not,
 - serve will be asked for the result, and
 - from the result the tree identifier of the root will be computed
   in memory and the root set to that value, as absent; when building
   in compatible mode, the necessary rehashing might have to fetch
   certain artifacts, but this is accepted, as the main intended
   use case is a native build.

If a concrete computed root refers to a base repository with absent
target root,
 - the client will ask serve about the flexible variables of the
   specified target, and
 - with this information will compute locally the cache key and
   inspect the local target-level cache. If not there, the root will
   be built, installed to a local temporary directory and imported
   into the git cas.

In the remaining case of a concrete computed root with concrete
target root of the referred base repository, the cache key can be
computed locally and a local check for a cache hit can be performed;
in this way, unnecessary IO-operations are avoided. If no cache
hit is found, the target will be built, installed to a temporary
directory and imported into the git cas.
