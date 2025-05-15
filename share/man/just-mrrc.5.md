% JUST-MRRC(5) | File Formats Manual

NAME
====

just-mrrc - The format of the configuration used by **`just-mr`**(1)

DESCRIPTION
===========

The file is read as JSON. Any other serialization describing the same
JSON object is equivalent. We assume, that in JSON objects, each key
occurs at most once; it is implementation defined how repetitions of the
same key are treated.

Location objects
----------------

A *location* is a JSON object with the keys *`"root"`*, *`"path"`*, and
*`"base"`*.

The value for key *`"root"`* is either *`"workspace"`*, *`"home"`*, or
*`"system"`*, which have the following meanings:

 - *`"workspace"`* refers to the root of the workspace in which the
   `just-mr` invocation was issued (not the workspace of the requested
   main repository). This location root is ignored if **`just-mr`** was not
   invoked from inside a workspace.

 - *`"home"`* refers to the user's home directory.

 - *`"system"`* refers to the system root *`/`*.

The value for key *`"path"`* is the relative path of the item to locate
within the location root.

The value for key *`"base"`* is a relative path within the location
root. This path is only relevant for locations of config files. If such
a config file contains relative paths, those will be resolved relative
to the specified base. If omitted, the default value *`"."`* is used.

The just-mrrc format
--------------------

The just-mrrc is given by a JSON object.

 - The value for the key *`"config lookup order"`* is a JSON list of
   location objects, specifying where to look for multi-repository
   configurations (see **`just-mr-repository-config`**(5) for more
   detail). The lookup is performed in the same order the location
   objects appear in the list.

 - The value for the key *`"absent"`*, if provided, is a JSON list
   of location objects to search for a file specifying the list of
   absent repositories.

 - The value for the key *`"local build root"`* is a single location
   object, specifying the path to use as the local build root. For more
   details, see **`just-mr`**(1).

 - The value for the key *`"checkout locations"`* is a single location
   object, specifying the path to the file describing checkout locations
   and additional mirror locations. For more details, see **`just-mr`**(1).

 - The value for the key *`"distdirs"`* is a JSON list of location
   objects, specifying where to look for distribution files (usually
   collected via the subcommand **`fetch`**). The lookup is performed in
   the same order the location objects appear in the list. For more
   details, see **`just-mr`**(1).

 - The value for the key *`"just"`* is a single location object,
   specifying the path to the **`just`** binary to use for execution, if
   **`just-mr`** is used as a launcher.

 - The value for the key *`"git"`* is a single location object,
   specifying the path to the git binary to use in the instances when
   **`just-mr`** needs to shell out.

 - The value for the key *`"local launcher"`*, if given, is list of
   strings setting the default for local launcher for **`just-mr`**;
   command-line arguments take precedence over the value in the
   configuration file. If the key *`"local launcher"`* is absent, the
   default *`["env", "--"]`* is assumed.

 - The value for the key *`"log limit"`*, if given, sets the default
   value for the log limit, that can be overridden by the command-line
   options.

 - The value for the key *`"restrict stderr log limit"`*, if given,
   sets the default value for the restriction of the log limit on
   console; this value can be overridden by the command-line option.

 - The value *`"log files"`*, if given, has to be a list of location
   objects, specifying additional log files, on top of those specified
   on the command line.

 - The value for the key *`"remote execution"`* is a JSON object specifying the
   remote execution options for **`just-mr`**.  
   For subkey *`"address"`* the value is a string specifying the remote
   execution address in a NAME:PORT format.  
   For subkey *`"compatible"`* the value is a flag which specifies whether the
   remote endpoint uses the original remote execution protocol.  
   Each subkey value can be overwritten by its corresponding command-line
   argument.

 - The value for the key *`"remote serve"`* is a JSON object specifying the
   remote serve options for **`just-mr`**.  
   For subkey *`"address"`* the value is a string specifying the remote
   serve address in a NAME:PORT format.  
   Each subkey value can be overwritten by its corresponding command-line
   argument.

 - The value for the key *`"authentication"`* is a JSON object specifying
   client-side authentication options for **`just-mr`**.  
   For subkey *`"ca cert"`* the value is a single location object
   specifying the path to a TLS CA certificate.
   For subkey *`"client cert"`* the value is a single location object specifying
   the path to a TLS client certificate.  
   For subkey *`"client key"`* the value is a single location object specifying
   the path to a TLS client key.  
   Each subkey value can be overwritten by its corresponding command-line
   argument.

 - The value for the key *`"remote-execution properties"`*, if
   provided, has to be a list of strings. Each entry is forwarded
   as `--remote-execution-property` to the invocation of the build
   tool, if **`just-mr`** is used as a launcher.

 - The value for the key *`"max attempts"`*, if provided, has
   to be a number. If a remote procedure call (rpc) returns
   `grpc::StatusCode::UNAVAILABLE`, that rpc is retried at most
   this number of times.

 - The value for the key *`"initial backoff seconds"`*, if provided,
   has to be a number. Before retrying an rpc the second time, the
   client will wait the given amount of seconds plus a jitter, to
   better distribute the workload.

 - The value for the key *`"max backoff seconds"`*, if provided,
   has to be a number. Normally, on subsequent retries, the backoff
   time is doubled; this number specifies the maximal time between
   attempts of an rpc, not counting the jitter.

 - The value for the key *`"just files"`* is a JSON object. The keys correspond
   to options that some **`just`** subcommands accept and require a file as
   argument. For each key, the value is a list of location objects. When
   **`just-mr`** is used as a launcher and the invoked subcommand is known to
   support this option, this option is set in the **`just`** invocation with
   the first matching entry, if any. The supported options are *`"config"`*
   and *`endpoint-configuration`*.

 - The value for the key *`"just args"`* is a JSON object. Its keys are
   **`just`** subcommands and its value is a JSON list of strings. For the
   corresponding subcommand, these strings are prefixed to the **`just`**
   argument vector (after all other options provided through the rc file),
   if **`just-mr`** is used as a launcher.

 - The value for the key *`"rc files"`*, if given, is a list of
   location objects. For those location objects that refer to
   an existing file, this file is read as an additional rc file
   overlaying the given rc file in the specified order; the value
   of `"rc files"` in the overlaying files is ignored.  
   In this way, an rc file committed to a repository can be allowed
   to set a sensible configuration, remote-execution and serve end
   points, etc. This is particularly useful when simultaneously
   working on several projects with different settings.

 - The value for the key *`"invocation log"`* is a JSON object
   specifying how each invocation should be logged. It supports
   the following subkeys.
   - *`"directory"`* A simple location object specifying under which
     directory the invocations should be logged. If not given, no
     invocation logging will happen.
   - *`"project id"`* A path fragment specifying the subdirectory of
     the given directory; if not specified, `"unknown"` will be used.
     Under this directory, for each invocation, an invocation-log
     directory will be created. The *`"project id"`* is given as
     a separate subkey, in order to allow workspace-specific rc
     files that are merged in to set this value.
   - *`"invocation message"`* An additional info message to be shown,
     followed by the base name of the invocation logging directory.
   - *`"metadata"`* A file name specifying where in the invocation-log
     directory the metadata file should be stored. If not given,
     no metadata file will be written. See **`just-profile`**(5) for
     details of the format.
   - *`"context variables"`* An optional list of environment variables,
     which are captured at invocation time and stored as key-value pairs
     in the metadata file. These variables do not affect the build in
     any way. Instead, they are supposed to provide useful context
     information about the invocation like username, merge-request ID or
     source branch.
   - *`"--dump-graph"`* A file name specifying the file in
     the invocation-log directory for an invocation-specific
     `--dump-graph` option.
   - *`"--dump-plain-graph"`* A file name specifying the file
     in the invocation-log directory for an invocation-specific
     `--dump-plain-graph` option.
   - *`"--dump-artifacts-to-build"`* A file name specifying the
     file in the invocation-log directory for the invocation-specific
     `--dump-artifacts-to-build` option.
   - *`"--dump-artifacts"`* A file name specifying in the file
     in the invocation-log directory for the invocation-specific
     `--dump-artifacts` option.
   - *`"--profile"`* A file name specifying the file in invocation-log
     directory for an invocation-specific `--profile` option.


EXAMPLE
=======

An example just-mrrc file could look like the following:

``` jsonc
{ "rc files":
  [ {"root": "workspace", "path": "rc.json"}
  , {"root": "workspace", "path": "etc/rc.json"}
  ]
, "config lookup order":
  [ {"root": "workspace", "path": "repos.json"}
  , {"root": "workspace", "path": "etc/repos.json"}
  , {"root": "home", "path": ".just-repos.json"}
  , {"root": "system", "path": "etc/just-repos.json"}
  ]
, "invocation log":
  { "directory": {"root": "system", "path": "var/opt/just-invocation"}
  , "metadata": "metadata.json"
  , "--dump-graph": "graph.json"
  , "--profile": "profile.json"
  }
, "absent":
  [ {"root": "workspace", "path": "etc/absent.json"}
  , {"root": "home", "path": ".just-absent"}
  ]
, "local build root": {"root": "home", "path": ".cache/just"}
, "checkout locations": {"root": "home", "path": ".just-local.json"}
, "local launcher": ["env", "--"]
, "log limit": 5
, "restrict stderr log limit": 4
, "log files": [{"root": "home", "path": ".log/just/latest-invocation"}]
, "distdirs": [{"root": "home", "path": ".distfiles"}]
, "just": {"root": "system", "path": "usr/bin/just"}
, "git": {"root": "system", "path": "usr/bin/git"}
, "remote execution": {"address": "10.0.0.1:8980"}
, "remote-execution properties": ["image:development-v1.2.3"]
, "just args":
  { "build": ["-J", "64"]
  , "install": ["-J", "64", "--remember"]
  , "install-cas": ["--remember"]
  }
, "just files":
  { "config":
    [ {"root": "workspace", "path": "etc/config.json"}
    , {"root": "home", "path": ".just-config"}
    ]
  }
}
```

See also
========

**`just-mr`**(1),
**`just-mr-repository-config`**(5)
