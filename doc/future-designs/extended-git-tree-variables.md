# Extended Variables in Actions for `"git tree"` roots

## Background

Justbuild is tightly coupled to `git`: it uses the same identifiers
as `git` to refer to files and directories. In this way, cache keys
for actions where the inputs come for `git` trees can be computed
without inspecting the respective files or directories. Due to
this tight coupling, `git` repositories get preferential treatment,
e.g., fetching them is built into `just-mr`.

Of course, adding support for every version-control system that might
be relevant does not scale. Instead, `"git tree"` repositories are
supported as a generic way to support all (other) version-control
systems; they are given by a `git` tree identifier and a command
that, when executed, produces this tree somewhere in the current
working directory&mdash;typically by checking out a specific version
using a different version-control system.

## Current Limitation

Calls to other version-control systems often require certain
environment variables to be set. E.g., for `ssh`-based authentication
typically one would set `SSH_AUTH_SOCK`. If proxy settings should
be honored, those would also be passed via appropriate environment
variables. All these environment variables have in common that
they need different values for different users; in particular, they
cannot be set in to fixed values in the action description.

## Proposed Change

As for `"git tree"` repositories the result of the fetch action is
determined ahead of time, it is not necessary to isolate the fetch
actions in the same way as we do with the build actions; if the
non-isolation misguides the fetch action, the verification of the
fetched tree will notice this and an error will be reported instead
of obtaining wrong results. Therefore, the following proposed
extension is safe.

The description of a `"git tree"` repository already contains
an optional field `"env"` (defaulting to the empty map) with a
string-string map specifying the environment in which the fetch
command is to be executed. We propose to extend the definition by an
additional optional field `"inherit env"`, that, if given contains
a list of strings thought of as names of environment variables.
For each of them, if they are set in the ambient environment in
which `just-mr` is invoked, they are passed with the value in the
ambient environment to the environment of the fetch command (taking
precedence over a possible fixed value set in the `"env"` map);
those not set in the ambient environment are ignored, leaving a
possible value set in `"env"` untouched.

## Approach not to be taken: Reference to the main workspace

It might seem tempting to allow a variable pointing to the
main workspace, arguing that in this way more complex fetch
logic could be achieved (by having a command like `["sh", "-c",
"${WORKSPACE}/bin/fetch.py example.com/foorepo"]`).

Such an approach is, however, problematic, as the definition is
no longer self contained but depends on the "main" repository, and
therefore does not compose if used as a transitive dependency, e.g.,
via `just-import-git`. Here it is important to note that this not
due to a limitation of the import tool, but due to the fact that
two repositories might refer to a different "main" repository in
their definition.

For cases where a more complex fetch logic is needed, we note that
the `repos.json` can be, and often is, a generated file anyway,
e.g., for the reasons described the `just-deduplicate-repos` man
page. For generated files it is not problematic if they contain
duplicate code. Therefore, inlining the logic into the individual
commands is acceptable can can be done by the entity (typically a
script) generating the actual `repos.json` from the descriptions
of which repositories to follow at which branches.

If complex logic is needed to compute parts of the roots, like target
files, this is better accommodated by the "computed roots" proposal.
