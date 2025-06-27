Built-in rules
==============

Targets are defined in `TARGETS` files. Each target file is a single
`JSON` object. If the target name is contained as a key in that object,
the corresponding value defines the target; otherwise it is implicitly
considered a source file. The target definition itself is a `JSON`
object as well. The mandatory key `"type"` specifies the rule defining
the target; the meaning of the remaining keys depends on the rule
defining the target.

There are a couple of rules built in, all named by a single string. The
user can define additional rules (and, in fact, we expect the majority
of targets to be defined by user-defined rules); referring to them in a
qualified way (with module) will always refer to those even if new
built-in rules are added later (as built-in rules will always be only
named by a single string).

The following rules are built in. Built-in rules can have a special
syntax.

`"export"`
----------

The `"export"` rule evaluates a given target in a specified
configuration. More precisely, the field `"target"` has to name a single
target (not a list of targets), the field `"flexible_config"` a list of
strings, treated as variable names, and the field `"fixed_config"` has
to be a map that is taken unevaluated. It is a requirement that the
domain of the `"fixed_config"` and the `"flexible_config"` be disjoint.
The optional fields `"doc"` and `"config_doc"` can be used to describe
the target and the `"flexible_config"`, respectively.

To evaluate an `"export"` target, first the configuration is restricted
to the `"flexible_config"` and then the union with the `"fixed_config"`
is built. The target specified in `"target"` is then evaluated. It is a
requirement that this target be untainted. The result is the result of
this evaluation; artifacts, runfiles, and provides map are forwarded
unchanged.

The main point of the `"export"` rule is, that the relevant part of the
configuration can be determined without having to analyze the target
itself. This makes such rules eligible for target-level caching
(provided the content of the repository as well as all reachable ones
can be determined cheaply). This eligibility is also the reason why it
is good practice to only depend on `"export"` targets of other
repositories.

`"install"`
-----------

The `"install"` rules allows to stage artifacts (and runfiles) of other
targets in a different way. More precisely, a new stage (i.e., map of
artifacts with keys treated as file names) is constructed in the
following way.

The runfiles from all targets in the `"deps"` field are taken; the
`"deps"` field is an evaluated field and has to evaluate to a list of
targets. It is an error, if those runfiles conflict.

The `"files"` argument is a special form. It has to be a map, and the
keys are taken as paths. The values are evaluated and have to evaluate
to a single target. That target has to have a single artifact or no
artifacts and a single run file. In this way, `"files"` defines a stage;
this stage overlays the runfiles of the `"deps"` and conflicts are
ignored.

Finally, the `"dirs"` argument has to evaluate to a list of pairs (i.e.,
lists of length two) with the first argument a target name and the
second argument a string, taken as directory name. For each entry, both,
runfiles and artifacts of the specified target are staged to the
specified directory. It is an error if a conflict with the stage
constructed so far occurs.

Both, runfiles and artifacts of the `"install"` target are the stage
just described. An `"install"` target always has an empty provides map.
Any provided information of the dependencies is discarded.

`"generic"`
-----------

The `"generic"` rules allows to define artifacts as the output of an
action. This is mainly useful for ad-hoc constructions; for anything
occurring more often, a proper user-defined rule is usually the better
choice.

The `"deps"` argument is evaluated and has to evaluate to a list of
target names. The runfiles and artifacts of these targets form the
inputs of the action. Conflicting definitions for individual paths
are not an error and resolved by giving precedence to the artifacts
over the runfiles; conflicts within artifacts or runfiles are
resolved in a latest-wins fashion using the order of the targets in
the evaluated `"deps"` argument. However, the input stage obtained
by those resolution rules has to be free of semantic conflicts.

The fields `"cmds"`, `"cwd"`, `"sh -c"`, `"out_dirs"`, `"outs"`, and `"env"`
are evaluated fields where `"cmds"`, `"out_dirs"`, and `"outs"`
have to evaluate to a list of strings, `"sh -c"` has to evaluate to
a list of strings or `null`, `"env"` has to evaluate to a map
of strings, and `"cwd"` has to evaluate to a single string naming a non-upwards
relative path. During their evaluation, the functions `"outs"` and
`"runfiles"` can be used to access the logical paths of the artifacts
and runfiles, respectively, of a target specified in `"deps"`. Here,
`"env"` specifies the environment in which the action is carried
out. `"out_dirs"` and `"outs"` define the output directories and
files, respectively, the action has to produce; these path are read
relative to the action root. Since some artifacts
are to be produced, at least one of `"out_dirs"` or `"outs"` must
be a non-empty list of strings. It is an error if one or more paths
are present in both the `"out_dirs"` and `"outs"`. Finally, the
strings in `"cmds"` are extended by a newline character and joined,
and command of the action is the result of evaluating the field
`"sh -c"` (or `["sh", "-c"]` if `"sh -c"` evaluates to `null` or
`[]`) extended by this string; the command is executed in the
subdirectory of the execution root specified by `"cwd"`.

The artifacts of this target are the outputs (as declared by
`"out_dirs"` and `"outs"`) of this action. Runfiles and provider map are
empty.

`"file_gen"`
------------

The `"file_gen"` rule allows to specify a file with a given content. To
be able to accurately report about file names of artifacts or runfiles
of other targets, they can be specified in the field `"deps"` which has
to evaluate to a list of targets. The names of the artifacts and
runfiles of a target specified in `"deps"` can be accessed through the
functions `"outs"` and `"runfiles"`, respectively, during the evaluation
of the arguments `"name"` and `"data"` which have to evaluate to a
single string.

Artifacts and runfiles of a `"file_gen"` target are a singleton map with
key the result of evaluating `"name"` and value a (non-executable) file
with content the result of evaluating `"data"`. The provides map is
empty.

`"tree"`
--------

The `"tree"` rule allows to specify a tree out of the artifact stage of
given targets. More precisely, the deps field `"deps"` has to evaluate
to a list of targets. For each target, runfiles and artifacts are
overlayed in an artifacts-win fashion and the union of the resulting
stages is taken; it is an error if conflicts arise in this way. The
resulting stage is transformed into a tree. Both, artifacts and runfiles
of the `"tree"` target are a singleton map with the key the result of
evaluating `"name"` (which has to evaluate to a single string) and value
that tree.

`"tree_overlay"` and `"disjoint_tree_overlay"`
----------------------------------------------

The rules `"tree_overlay"` and `"disjoint_tree_overlay"` allow to
define a tree as the overlay of the trees given by the artifact
stages of a list of given targets. More precisely, the field `"deps"`
has to evaluate to a list of targets. For each target, a tree is
formed from its artifact stage. A new tree is defined as the tree
overlay, or disjoint tree overlay, of those trees. Both, artifacts
and runfiles of the target are a singleton map with key the result
of evaluating the field `"name"` which has to evaluate to a single
string and value that (disjoint) tree overlay.


`"symlink"`
------------

The `"symlink"` rule allows to specify a non-upwards symbolic link with a
given link target. To be able to accurately report about file names of
artifacts or runfiles of other targets, they can be specified in the field
`"deps"` which has to evaluate to a list of targets. The names of the
artifacts and runfiles of a target specified in `"deps"` can be accessed
through the functions `"outs"` and `"runfiles"`, respectively, during the
evaluation of the arguments `"name"` and `"data"` which have to evaluate to
a single string.

Artifacts and runfiles of a `"symlink"` target are a singleton map with
key the result of evaluating `"name"` and value a non-upwards symbolic link
with target path the result of evaluating `"data"` (which must evaluate to
a non-upwards path). The provides map is empty.

`"configure"`
-------------

The `"configure"` rule allows to configure a target with a given
configuration. The field `"target"` is evaluated and the result of the
evaluation must name a single target (not a list). The `"config"` field
is evaluated and must result in a map, which is used as configuration overlay
for the given target.

This rule uses the given configuration overlay to modify the current
environment for evaluating the given target, and thereby performs a
configuration transition. It forwards all results
(artifacts/runfiles/provides map) of the configured target to the upper
context. The result of a target that uses this rule is the result of the
target given in the `"target"` field (the configured target).

As a full configuration transition is performed, the same care has to be
taken when using this rule as when writing a configuration transition in
a rule. Typically, this rule is used only at a top-level target of a
project and configures only variables internally to the project. In any
case, when using non-internal targets as dependencies (i.e., targets
that a caller of the `"configure"` potentially might use as well), care
should be taken that those are only used in the initial configuration.
Such preservation of the configuration is necessary to avoid conflicts,
if the targets depended upon are visible in the `"configure"` target
itself, e.g., as link dependency (which almost always happens when
depending on a library). Even if a non-internal target depended upon is
not visible in the `"configure"` target itself, requesting it in a
modified configuration causes additional overhead by increasing the
target graph and potentially the action graph.
