# Tree Overlay Actions

## Introduction

Our build tool has tree objects as first-class citizens. Trees
can be obtained as directory outputs of actions, as well as by an
explicit tree constructor in a rule definition taking an arrangement
of artifacts and constructing a tree from it. Trees are handled as
opaque objects, which has two advantages.
- From a technical point, this allows passing through potentially
  large directories by simply passing on a single identifier.
- From a user point of view, this improves maintainability, as a
  certain target can already claim certain subtrees in its artifacts
  or runfiles, so that staging conflicts that might arise from a
  latter addition of artifacts are already detected now.

However, there are some use cases not covered by this way of handling
trees. E.g., when creating disk images, it might be desirable to add
project-specific artifacts to a tree obtained as directory output
of an action calling a foreign build system. Of course, there need
to be some out-of-band understanding where artifacts can be placed
without messing up the original tree, but often this is the case,
despite this being hard to formulate in a way that can be verified
by a build system; however, it is easy for a build system to verify
after the fact (i.e., once the trees are computed) that certain
trees do not conflict on any path. Such an after-the-fact check is
often maintainable enough and still guarantees that no files needed
for the disk image to work get lost during the build process.

A similar need for overlaying trees might occur when a third-party
library is built using a foreign build system and, in order to keep
the description maintainable over updates, the include files are
collected as a whole directory.

## Proposed Changes

We propose to add a new type of (in-memory) action `TREE_OVERLAY` that
rules can use to construct new trees out of existing ones by overlaying
the contents; we propose to also a variant `DISJOINT_TREE_OVERLAY`
that enforces that the overlayed trees do not conflict on any path.
For ad-hoc constructions, we also add a built-in rule `tree_overlay`
reflecting this additional action constructors. The following
sections describe the needed changes in detail.

### Action graph data structure: new action of overlaying trees

Currently, the action graph is given by
- `"actions"`, describing how new artifacts can be obtained by
  running a command in a directory given by arranging existing
  artifacts in a specified way,
- `"blobs"`, strings that can later be referenced as "known" artifacts
  through their content-addressable blob identifier, and
- `"trees"`, directory objects given by an arrangement of already
  existing artifacts.

We propose to extend that data structure by introducing a new category
`"tree overlays"` mapping (intensional) names to their definition
as a list of existing tree artifacts, together with a bit indicating if
the build should be aborted if the overlayed trees conflict on any path.
The extensional value of such
a tree overlay is obtained by starting with the empty tree and,
sequentially in the given order, overlay the extensional value of
the defining artifacts. Here, the overlay of one tree by another is
a tree where the maximal paths are those of the second tree together
with those of a first tree that are not in conflict with any from
the second; the artifact at such a maximal path is the one at that
place in the second tree if the second tree contains this maximal
path, otherwise the artifact at this position in the first tree.

We keep the design that the action graph is obtained in the analysis
phase as the union of the graph parts of the analysis results of the
individual targets. Therefore, the analysis result of an individual
target will also contain (besides artifacts, runfiles, provides
map, actions, blobs, and trees) a collection of tree overlays.

### Computation of `"tree overlays"` in the presence of remote execution

The evaluation of `"tree overlays"` will happen in memory in the `just`
process. To do so, the actual tree objects have to be inspected, in
fact downwards for all common paths. In particular, as opposed to
the remaining operations, trees in this operation cannot be passed
on as opaque objects by simply copying the identifier. In the case
of remote execution that means that the respective tree objects have
to be fetched; to avoid unnecessary traffic, only the needed tree
objects will be fetched without the blobs or tree objects outside
common paths, even if that means that those objects cannot be put
into the local CAS (as that would violate the tree invariant). In
any case, when adding the new tree objects that are part of the
overlayed tree, we have to ensure we add them to the applicable
CAS in topological order, in order to keep the tree invariant.

### Additional function in rule definition: `TREE_OVERLAY`

In the defining expressions of rules, two additional constructors
`TREE_OVERLAY` and `DISJOINT_TREE_OVERLAY` are added that (like
`ACTION`, `BLOB`, and `TREE`) can be used to describe parts of the
action graph. These constructors have one argument `"deps"` which
has to evaluate to a list of tree-conflict&mdash;free mappings
of strings to artifacts, also called "stages". The result of this
function is a single artifact, the tree defined to be the overlay
or conflict-free overlay, respectively, of the trees corresponding
to the stages.

The reason we require stages to be passed to the new constructor
rather than artifacts that happen to be trees is twofold.
- We want to find malformed expressions already analysis time;
  therefore, we need to ensure not only that the arguments to the
  `"tree_overlays"` entry in the action graph are artifacts, but, in
  fact, tree artifacts. By requiring that implicit tree constructor
  we avoid accidental use of file outputs, as a location has to be
  explicitly specified.
- One the other hand, we expect that often the inputs are the
  artifacts of a dependency, which is naturally given as a stage
  via `DEP_ARTIFACTS`. So this form of definition is actually more
  convenient to use.

### Additional built-in function `tree_overlay`

To stay consistent with the idea that any build primitive also has
a corresponding built-in rule type, we also add additional built-in
rules `"tree_overlay"` and `"disjoint_tree_overlay"`. They have a
single field `"deps"` which expects a list of targets. Both, runfiles
and artifacts of the `"tree_overlay"` target are the tree overlays
or conflict-free tree overlay, respectively, of the artifacts of
the specified `"deps"` targets in the specified order.
