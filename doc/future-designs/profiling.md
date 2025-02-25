# Profiling builds

## Problem statement and use cases

There are several use cases where inspection of the internals of
a build can be provide additional insights.

### Time spent during the build

For large projects, build times can grow quite large. While increased
parallelism, e.g., via remote execution, helps to reduce build
times, it cannot reduce build time below that of the critical path.
So it is an important step in maintaining a code base to keep the
criticial path short, both in terms of number of actions, but also,
more importantly in terms of typical runtime of those actions. Such
a task requires profiling data of real-world builds.

### Test history

When running test actions, beside the time spent it is also important
to understand how a test status evolved over the history of the
main branch of a code base, in terms of pass/fail/flaky, possibly
also in comparison with test results on merge requests. In this
way, information on the reliability and usefulness of the test can
be deduced. It can also serve as basis for identifying when a test
broke on the head branch, helping to identify the offending commit.

### Build-invocation sharing

A different use case is to share a particular intersting build
invocation with a different developer working on the same project.
This can be a particularly interesting failure of a test (e.g.,
one that is flaky with extremely low error probability) or a
hard-to-understand build error. Having all the information about
that particular build invocation can facilitate cooperation, even
with remote sites.

## Considerations

In the described profiling use cases, the importance is evolution
over time. For build steps, the relevant data is the time taken and
the frequency that build step has to run. For steps that are only
run very infrequently, usually the time taken is not that important.
But for both those pieces of information, the evolution (typically
along the main deveopment branch of the code base) is relevant.
 - If a build step suddenly has to be run a lot more often, and hence
   gets more relevant for the overall duration of the build, this
   can be a hint that a change in the dependency structure added
   some hot parts of the code base to the transitive dependencies.
   Comparison of the dependency structure before and after that
   change can help to restructure the code in a way more favourable
   for efficient builds.
 - The the time for a single build step quickly increases over the
   history of the main branch, that is a candidate for a refactoring
   in the near future. Thus, by appropriately monitoring where the
   time for individual build steps increases, appropriate action
   can be taken before the time becomes a problem.

Depending on the use case, a different form of accumulation of
the observed data is needed. As it is infeasible to cover all the
possible derived statistics, we instead focus only on providing
the raw data of a single invocation. We leave it to the user create
a suitable tool collecting the invocation data, possibly from
different machines, and computing the relevant statistics as well
as presenting it in a friendly way.

In order to keep things simple, for the time being, we only support
one form of outputting the needed data: writing it to a file. The
collection and transfer to a central database is a task that can be
solved by a separate tool. Nevertheless, we chose paths in such a
way, that log files can be written to a network file system, which
is one possible way of central collection.

In any case, we only output files in a machine-readable format.

Collection of build data, if desired, should be done routinely.
For the build-sharing use case it is not known ahead of time,
which invocation will be the intersting one. Also, statistics over
invocations are much more informative, if the data is complete.
Therefore, the build-data collection should be configured in a
configuration file. The only tool we have that routinely reads a
configuration file is `just-mr`. As this is also routinely used
as a launcher for `just`, its configuration file is a good place
to configure build-insight logging. Following the current design,
we let `just-mr` do all the necessary set up and extend `just`
only minimally.

## Proposed Changes

### `just` to receive a `--profile` option

The build tool `just` will get a new option `--profile` with one
file name as parameter. This option is accepted by all `just`
subcommands that do at least analysis. After completing the attempt
for the requested task, it will write to the specified file name
a file containing information about that attempt. This will also
happen if no build was attempted despite being requested, e.g., due
to failure in analysis. The file contains a single JSON object, with
the following key (and more keys possibly added in the future).
 - The key `"exit code"` contains the exit value of the `just`
   process; this allows easy filtering on the build and test results.
 - The key `"target"` contains the target in full-qualified form.
   The reason we include the target is that `just` allows to also
   deduce it from the invocation context (like working directory).
 - The key `"configuration"` contains the configuration in which
   this target is built. The reason this is included is that it
   is a value derived not only from the command line but also from
   the context of a file (given via `-c`) possibly local on the
   machine `just` was run.
 - The key `"actions"` contains a JSON object with the keys being
   precisely the key identifiers of the actions attempted to run.
   The action identifiers are the same as in the `--dump-graph` or
   `--dump-plain-graph` option; it is recommended to also run one
   of those options to have additional information on what those
   actions are. For each action attempted to run, the following is
   recorded (with possible additions in the future).
    - The exit code, as number.
    - The blob/tree identifier of all declared outputs, as well
      those of stdout/stderr (if given). This is the information
      needed for sharing a build invocation. However, it is also
      useful for running additional analysis tools over the actions
      causing lengthy builds; those additional analysis tools can
      answer questions like "Which input files precisely are the
      ones that change frequently?" and "Are those header files
      even used for that particular compilation step?". In case of
      tests, the collected outputs can be used to compare succesfull
      and failing runs of a flaky test.
    - Wether the action was cached.
    - For non-cached actions, the duration of the action, as
      floating-point seconds. If possible, that information is taken
      from the execution response (following the key `result` to
      the `ExecutedActionMetaData`); in this case, also additional
      meta data is recorded. If obtaining the meta data that way
      is not possible, the wall-clock time between starting and
      completing the action can be taken; in this case, the fact
      that this fallback was taken is also recorded.

Additionally, `just` will receive a flag `--async-profile`,
defaulting to false. If given, the writing of the action graphs (if
`--dump-graph` or `--dump-plain-graph` is given) as well the profile (if
`--profile` is given) are asynchronously written in a forked-off
subprocess (note that, at the moments those files are written,
`just` is single threaded) and the main process does not wait for
the child (i.e., follows a fork-and-forget strategy); in this case,
profiling is done only on a best-effort basis. Using asynchronous
profile logging, the regular developer workflow is not affected by
the overhead of writing these detailed machine-readable logs.

### `just` options `--dump-graph` and `--dump-plain-graph` become cummulative

The options `--dump-graph` and `--dump-plain-graph` change their
semantics from "latest wins" to be cummulative, in the same way as
`-f` is already. That is, if these options are given several times
then `just` will also write the graph file to several destinations.
In this way, it is possible to have an invocation-specific logging
of the action graph without interfering with other graph logging.

### `just-mr` to support passing unique log options on each invocation

The configuration file for `just-mr` will be extended by an
additional entry `"invocation log"`. This entry, if given, is a
JSON object; rc-file merging is done on the individual entries of
the `"invocation log"` object. It will support the following keys.
 - `"directory"` A single location object specifying the directory
   under which the invocation logging will happen. If not given, no
   invocation logging will happen and the other keys are ignored.
 - `"project id"` A path fragment (i.e., a non-empty string, different
   from `"."` and `".."`, without `/` or null character). If not
   given, `unknown` will be assumed.
 - `"--profile"`, `"--dump-graph"`, `"--dump-plain-graph"`. Each a
   path fragment specifying the file name for the profile file, the
   graph file, or plain-graph file, respectively. If not given, the
   respective file will not be requested in the invocation of `just`.
 - `"meta data"` A path fragment specifying the file name of the
   meta-data file. If not give, no meta-data file will be created.
 - `"async"` a boolean. If not given, `false` will be assumed.

If invocation logging is requested, `just-mr` will create for each invocation
a directory `<prefix>/<project-id>/<YYYY-mm-DD-HH:MM>-<uuid>` where
 - `<prefix>` is the directory specified by the `"directory"`
   location object.
 - `<project-id>` is the value specified by the `"project id"`
   field (or `unknown`). The reason that subdirectory is specified
   separately is to allow projects to set their project id in the
   committed code base (via rc-file merging) wheras the general
   logging location can be specified in a machine-specific way.
 - `<YYYY-mm-DD-HH:MM>` is the UTC timestamp (year, moth, day,
   hours, minutes) of the invocation and `<uuid>` is a universally
   unique id for that invocation (following RFC9562). The reason we
   prefix the UUID with a time stamp is allow a simple time-based
   collection and clean up of the log data.
Inside this directory the requested files with the specified file
names will be created, in the case of `"--profile"`, `"--dump-graph"`,
and `"--dump-plain-graph"` by passing the appropriate options to
the invocation of `just`. If `"async"` is set to `true`, the option
`--async-profile` will be given as well to all targets building (i.e.,
`build`, `install`, and `rebuild`).

The meta-data will be written just by `just-mr` itself, just before
doing the `exec` call. The file contains a JSON object with keys
 - `"time"` The time stamp in seconds since the epoch as
   floating-point number.
 - `"cmdline"` The command line, as vector of strings, that `just-mr`
   is about to `exec` to.
 - `"configuration"` The blob-identifier of the multi-repository
   configuration; in this way, the actual repository configuration
   can be conveniently backed up. In this way, analysis with respect
   to the source tree is possible.
