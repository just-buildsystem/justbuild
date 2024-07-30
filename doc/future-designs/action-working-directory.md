# Support for action working directory inside the execution root

## Current state and shortcomings

In the spirit of bootstrapable builds, toolchains (like the production
compiler to be used) are often not installed into the execution
environment but rather brought in as a dependency of an action; the
execution environment then only contains a minimal bootstrapping
compiler. The same is true when using first-party tools that are
under active development; then the tool and the usage of the tool
need to be versioned together and the precise snapshot version need
to be (built from source and) used.

In order to accommodate custom toolchains while still not introducing
forbidden magic names for the user of the toolchain, the toolchain
and the sources using the toolchain are typically staged into parallel
directories. Command lines become most natural if executed on the
source directory. This is especially true for command lines (composed
from provided fragments) mixing references to artifacts with other
options, where a rewriting to be executed in the top-level directory
might be hard. Recently, for linking of CC binaries, this execution
in a subidrectory was
[achieved](https://github.com/just-buildsystem/justbuild/commit/89482c9850c46968ae6bbd61a9645b2d394ca53d#diff-d6fc028f89d97a6b2d3fb0ae7b069dac2cc000b7c8e3dfe3726d13d63d8e7c3cR1460)
by constructing a command of the form `cd work && ...` to be
executed by `sh`.

However, the remote-execution
protocol [supports](https://github.com/bazelbuild/remote-apis/blob/0d21f29acdb90e1b67db5873e227051af0c80cdd/build/bazel/remote/execution/v2/remote_execution.proto#L692)
specifying a subdirectory of the execution directory that acts as
working directory for executing the command. However, the `justbuild`
rule language currently does not allow specifying actions with
working directory a subdirectory of the execution root; nor does
local execution support this. As explained, supporting a working
directory deeper inside the action root would allow simpler and
more efficient rules; it would also fit into the general design
of `justbuild` to provide the user with the flexibility of remote
execution and model local execution following the principles of
remote execution.


## Proposed changes

In the rule language, the `"ACTION"` function will get a new (optional,
with default value `""`) field `"cwd"`. If given, the value has
to be a string specifying a non-upwards relative path (with `""`
interpreted as meaning the same directory). When an action is
executed, the working directory is the specified subdirectory of
the execution root. To ensure such a directory exists in the action
input, if `"cwd"` is specified and no input artifact has a path
that goes through the directory specified by `"cwd"`, then an empty
directory at the directory specified by `"cwd"` will implicitly be
added as additional action input.


Outputs of an action are still interpreted relative to the execution
root. In this way, the value of the action is an output stage and
has as keys precisely the values of `"outs"` and `"out_dirs"`.

This differs from the way the remote-execution protocol interprets
outputs; in the remote-execution protocol, they are interpreted
relative to the working directory, not relative to the execution
root (in particular, when the working directory is set to `foo`,
an output path like `../bar/baz.txt` is legitimate). Therefore,
when composing a message for remote-execution (`justbuild` acting
as a client) or receiving one (when run as `just execute`), the
corresponding path computation is done to transform the request
into the internal action representation.

## Backwards compatibility

As the new field `"cwd"` defaults to take the same working as
execution directory, old action descriptions continue to have the
same semantics. So this extension is backwards compatible.
