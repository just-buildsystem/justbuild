# Support for upwards symbolic links

## Background

### Existing symlink support

Our build system already supports non-upwards relative symbolic links
as first-class objects. The reason for choosing this restriction was
that those can be placed anywhere inside an action directory without
that directory becoming dependent on the ambient system, regardless
if symbolic links are followed or inspected via readlink(2).

Additionally, `just-mr` supports resolving symlinks that are
entirely within a logical root. This allows to support roots where
symlinks are used to have file under version control only once; as
roots are internally represented in a content-addressable way, the
duplication implicit to the resolving comes at no extra cost.

### Level of enforcement

The restriction to only allow only non-upwards relative symlinks
is enforced
- in the output of actions, including output directories,
- in explicit tree references on `file` roots.

However, it is not enforced when using an explicit tree reference on
a `git` root as input. The reason is that we want to benefit from
the fact that trees are already hashed in git repositories so that
we can get values (and harvest cache hits) without ever looking at
that subdirectory.

### Dangling relative upwards symbolic links

While used rarely, dangling upwards relative symlinks do exist in
some projects, also for legitimate reasons. Being dangling, they
cannot be resolved in the root definition. Where such projects occur
as (transitive) dependency, some form of extended symlink handling
seems desirable.

## Proposed changes

### Extend definition language to support arbitrary relative symlinks

We extend the description language to allow handling relative
symlinks, including upward ones, in a safe way.

#### Extensional symlink level of a computed artifact

We associate a symlink-level with blobs and trees that do not
transitively contain absolute symlinks in the following way.
- Files and executable files have symlink level 0.
- The symlink level of a relative symlink is the number of (necessarily
  leading) `../` of the syntactical canonical form, when read as
  a relative path.
- The symlink level of a tree is the maximum of 0 and the symlink-levels
  of all (immediate) subobjects reduced by one.

#### The declared symlink level

In the description language a declared symlink level is associated
with each artifact; the invariant is that
- the declared level is always at least as big as the extensional level
  of the defined object, and
- the action directory of each action has declared level 0, ensuring
  that action directories do not refer to external sources.

In order to do so, we extend our target names for explicit symlink
and tree references to optionally (defaulting to 0) declare a
symlink level. This is done by allowing in the target name, an
additional dict as last entry, which may contain the key `"symlink
level"`; the value for that key is the declared level and has to
be a non-negative integer. For example,
- `["SYMLINK", null, "foo", {"symlink level": 3}]` is an explicit
  symlink reference to a symlink at `foo` in the current module with
  declared symlink level 3, and
- `["TREE", null, "bar", {"symlink level": 4}]` is an explicit tree
  reference to a tree located at `bar` in the current module with
  declared symlink level 4.
The reason for choosing a dict rather than a positional argument is
to be prepared for additional declared properties in the future,
should they become necessary.

We extend the definition of `"ACTION"` function available inside
rule definitions to declare a symlink level of output files and
output directories by allowing an optional map `"symlink level"`
with keys the names of the declared outputs (files and directories)
and values non-negative integers. The declared symlink level of
an action artifact is the value of the output path in that map; if
not found in that map, the declared value is 0; in this way, this
extension is backwards compatible. Moreover, we require that in
the input stage of an action, every artifact of declared symlink
level `n` is staged under at least `n` directories.

We also extend the `"SYMLINK"` function available inside rule
definitions to have an optional field `"symlink level"` specifying
the declared symlink level. The value, if specified, has to be a
non-negative integer and the default is 0.

Artifacts defined by the `"TREE"` function have as declared symlink
level the maximum of 0 and for each artifact of the defining stage
the difference of the symlink level of the artifact and the number
of directories it is staged under.

Artifacts defined by the `"BLOB"` function available inside rule
definitions have symlink level 0.

### Enforce correctness of symlink level

All computed artifacts (that are `KNOWN` artifacts once computed)
carry the actual symlink level as part of their data structure;
when serializing the artifact description, the symlink level is
reported if (and only if) it is not 0.

For inputs (explicit symlink and tree reference) we enforce that the
actual symlink level is not larger than the declared one. To enforce
this check while keeping the benefits of explicit tree references
in `git` roots, we keep a cache (in the form of a simple mapping on
disk) of tree identifiers to their symlink level. This map will be
garbage collected together with the repository roots (as described
in the "Garbage collection for Repository Roots" design).

We also enforce the correctness of the symlink level of action
outputs: for `"outs"`, if they are a symlink, the level is checked
and the action rejected if the actual level is larger than the
declared one. For `"out_dirs"`, the directory is rejected if the
actual symlink level is larger than the declared one. It then follows
that all action have actual symlink level 0 of the input stage.

### Extensional projection reduces symlink level

In evaluation of an export target, the intensional description with
the declared symlink level is replaced by the extensional description
using the extensional symlink level. By our construction, the
latter is less or equal than the former. Hence, this projections
allows at most _more_ builds which is in line with the properties
of export targets.
