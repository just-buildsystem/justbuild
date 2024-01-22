# Multiple rc files for `just-mr`

## Current situation and shortcomings

Upon startup, `just-mr` reads a single rc file to set various defaults,
including remote-execution endpoint and early arguments for the
various `just` subcommands, if used as a launcher. By default, the
rc file read is the file `~/.just-mrrc`, if it exists. A different
rc file can be specified via the `--rc` option. Moreover, by givining
`--norc` the reading of an rc file can be disabled entirely.

Using a global rc file works fine when working on a single project
or when only building locally. However, when working on several
projects, it quickly becomes necessary to switch between various
configurations of the build set up.

Different projects might have different default build configurations,
dispatch to different execution-property-specific endpoints, or
consider different repositories absent. Due to the ability to
specify a workspace-relative file name in the just-mr rc file these
different set ups can be managed by committing the respective files
in to the repository.

However, different projects, also use different build images,
reflected by different remote-execution properties; some might even
use different remote-execution end points or differ in the serve
end point they use (or whether a serve end point is used at all).
Especially for the remote-execution properties used, it is desirable
to version those in the repository. A change of the remote-execution
property usually reflects the use of a different build environment
and properly versioning such change is important when reconstructing
old binaries or when bisecting to find a breakage.

The current solution is to commit the respective rc file for `just-mr`
and expect the user to provide the respective `--rc` option. While
this works in principle, it causes additional inconvenience when
working deep inside a subdirectory of the project, and often causes
build attempts in unintended `just-mr` configurations due to a
forgotten or wrong `--rc` argument.

## Proposed changes

We propose to add a new field `"rc files"` that, if given, contains
a list of location objects. After reading the main rc file, those
location objects will be processed in the given order; if a file
exists at the specified location, it will be read as an additional
rc file, however ignoring a possibly given `"rc files"` field. If
values are specified in several rc files, the latest value wins.

Only after all the rc files are read, the resulting rc configuration
is evaluated. In particular, a later rc file can override an earlier
specification of the search order for the repository config, or
for individual `"just files"`.

In this way it is possible, by simply specifying the naming
conventions used, to automatically use the committed rc-files for
the project of the current working directory. The global rc file
can also specify useful defaults; if necessary, it is also possible
to specify hard overrides, by specifying a corresponding file in
the user's home directory at the very and of the list.
