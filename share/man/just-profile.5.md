% JUST-PROFILE(5) | File Formats Manual

NAME
====

just-profile - The format of profile files written by **`just-mr`**(1) and **`just`**(1)

DESCRIPTION
===========

If profiling is enabled through the *`"invocation log"`* key in
the **`just-mrrc`**(5) file, **`just-mr`**(1) can be told to write
a metadata file, and the launched **`just`**(1) process to write a
profile file. Both files contain a single JSON object.

Metadata file
--------------

The metadata file contains the following information.

- For the key *`"cmdline"`* the argument vector for the process it
  will **`execv`**(3) to. As with all launches by **`just-mr`**(1),
  the programm is the zeroth entry of the argument vector.

- For the key *`"configuration"`* the blob identifier of the
  **`just-repository-config`**(5) that is passed to the launched process,
  if such a config is passed.

- For the key *`"time"`* the time of the invocation in seconds since
  the epoch.

- For the key *`"context"`* a map of the environment variables requested
  to be included in the metadata file with the variable name as key and
  the value of the variable as value (see *`"context variables"`* field
  of the *`"invocation log"`* key in the **`just-mrrc`**(5) file).

Profile file
------------

The profile file contains the following information.

- For the key *`"subcommand"`* the subcommand that was executed.

- For the key *`"subcommand args"`* the positional arguments that were
  provided for that subcommand.

- For the key *`"target"`* the target that was analysed/built/installed, as
  full qualified name.

- For the key *`"configuration"`* the build configuration in which the
  target was analysed/built/installed.

- For the key *`"remote"`* an object describing the remote-execution
  configuration. The following keys are used.

  - If a remote-execution endpoint is used, for the key *`"address"`*,
    a string of the form `address:port` is given.

  - For the key *`"properties"`* the remote-execution properties are
    specified (naturally as an object).

  - For the key *`"dispatch"`* the dispatch list is given as an array
    of pairs (arrays of length two) of a property map and an endpoint
    as `address:port` string.

- For the key *`"exit code"`* the exit code of the **`just`**(1) process.

- For the key *`"analysis errors"`*, if present, a list of error messages
  describing each an error that occurred during analysis.

- For the key *`"actions"`* an object. For each action that was looked at
  in the build phase there is an entry, with the key being the action
  identifier; the identifier is the same as in the **`just-graph-file`**(5)
  that is written as a result of the `--dump-graph` option. The value is
  an object with the following entries.

  - For the key *`"cached"`* a boolean indicating if the action was taken
    from cache.

  - For the key *`"time"`* for non-cached actions the build time in seconds.

  - For the key *`"exit code"`* the exit code of the action, if not 0.

  - For the key *`"artifacts"`* an object with keys the output paths and values
    the hashes of the corresponding artifacts as hex-encoded strings.

  - For they keys *`"stdout"`* and *`"stderr"`* the hashes of stdout and stderr,
    respectively, as hex-encoded strings, provided the respective output is
    non-empty.

- For the key *`"start time"`* the start time of the invocation in
  seconds since the epoch.

- For the key *`"build start time"`* the start time of the build phase (if
  any) in seconds since the epoch.

- For the key *`"stop time"`* the stop time of the invocation in seconds
  since the epoch.

See also
========

**`just-mr-rc`**(5),
**`just-graph-file`**(5),
**`just-mr`**(1),
**`just`**(1)
