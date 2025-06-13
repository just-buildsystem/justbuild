# Invocation Logging and Profiling

For large projects, it can be helpful to find out which actions
make, e.g., the runs of the continuous integration slow. `just` has
an option `--profile` that instructs it to write a profile file to
the specified location on disk. That profile file contains (among
other things, see `just-profile(5)`) for each action that was
considered during the build whether it was cached, and, if not,
its run time. These actions can be associated to targets via the
`"origins"` filed in the action graph that can be obtained via the
`--dump-graph` option.

However, not only the run time of actions in a particular build
matters, but also how often an action has to be rerun because some
input changed. Therefore, we need to collect profiling data over many
invocations. To do so, `just-mr` honors an option `"invocation log"`
in its rc file. For that key, several subkeys can be specified with
the most important being `"directory"`; the location object defined
there specifies the directory where the logging happens (always on
the local machine, even if remote-execution is used). Inside this
directory, a subdirectory is used for each project; the project can
be specified by the subkey `"project id"`. Typically, setting the
project identifier is delegated to an rc file in the workspace.

So, if we have `~/.just-mrrc` with contents
``` json
{ "rc files": [{"root": "workspace", "path": "etc/rc.json"}]
, "invocation log": {"directory": {"root": "home", "path": "log/justbuild"}}
}
```
and the project contains an `etc/rc.json` with
``` json
{ "invocation log": {"project id": "hot-new-product"}}
```
then for each build triggered through `just-mr` a new subdirectory
of `~/log/justbuild/hot-new-product` will be created. The name
of that subdirectory consists of date and time, followed by a
universally-unique identifier. In particular, parallel invocations
are no problem, even if invocation logging is activated. The
prefixing with date and time allows simple aggregation or cleanup
over fixed periods of time (like monthly, daily, hourly, etc).

Of course, just creating an empty directory for each invocation
of `just-mr` is not very useful. Therefore, more subkeys can
be specified.

 - `"--profile"` specifies the file name within the invocation-log
   directory to be used when generating the `--profile` option in
   the command line for the `just` invocation.
 - `"--dump-graph"` does the same for the `--dump-graph` option.
 - `"--dump-artifacts-to-build"` does the same for the
   `--dump-artifacts-to-build` option.
 - `"--dump-artifacts"` does the same for the `--dump-artifacts`
   option; while not directly useful for profiling, browsing the
   final artifacts (including the test logs) can be useful to
   understand a particular invocation.
 - `"metadata"` specifies a file name inside the invocation directory
   a metadata file should be written by `just-mr`; that file
   contains, in particular, the full command line that is executed
   and the blob identifier of the repository configuration file
   used in that invocation of `just` (if any).
 - `"context variables"` specifies a list of environment variables
   for which the value should be recorded in the metadata file;
   while `just` is designed to deliberately ignore environment
   variables for the build, environment variables can be used to
   communicate some context for the invocation, especially when
   run on a CI system. This can later also be used for an analysis
   based on a more fine-grained sharding.

So, if invocation logging is desired, the relevant part of a typical
`~/.just-mrrc` file could look as follows.
``` json
{ "rc files": [{"root": "workspace", "path": "etc/rc.json"}]
, "invocation log":
  { "directory": {"root": "home", "path": "log/justbuild"}
  , "metadata": "meta.json"
  , "context variables": ["CI_MERGE_REQUEST_IID", "CI_COMMIT_SHA"]
  , "--profile": "profile.json"
  , "--dump-graph": "graph.json"
  , "--dump-artifacts-to-build": "to-build.json"
  , "--dump-artifacts": "artifacts.json"
  }
}
```
If some shared infrastructure (like a network file system) is
available, it usually is a good idea to choose for `"directory"`
a `"system"` path rather than a `"home"` one. In any case, it is
advisable to set up some form of cronjob to rotate the invocation
logs, as they can get quite large.

Of course, the main motivation for invocation logging is doing
statistical analysis later. However, it can also be useful to browse
through the most recent invocations, looking, e.g., at failed actions
and their output, or actions that took particularly long. In the
`justbuild` source tree, under `doc/invocations-http-server` there
is a simple application serving one directory of invocation logs,
i.e., the logs for one particular project id.
