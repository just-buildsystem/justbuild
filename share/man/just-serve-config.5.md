% JUST SERVE CONFIG(5) | File Formats Manual

NAME
====

**`just`** **`serve`** configuration - The format of the configuration used by
the **`serve`** subcommand of **`just`**(1)

DESCRIPTION
===========

The file is read as JSON. Any other serialization describing the same
JSON object is equivalent. We assume, that in JSON objects, each key
occurs at most once; it is implementation defined how repetitions of the
same key are treated.

The just-serve configuration format
-----------------------------------

The configuration file is given by a JSON object.

 - The value for the key *`"local build root"`* is a string specifying the path
   to use as the root for local CAS, cache, and build directories. The path will
   be created if it does not exist already. 

 - The value for the key *`"repositories"`* is a list of strings specifying
   paths to Git repositories for **`just`** **`serve`** to use as additional
   object lookup locations. The paths are to be used in the order given and
   only if requested objects are not found in the local build root.  

 - The value for the key *`"logging"`* is a JSON object specifying logging
   options.  
   For subkey *`"files"`* the value is a list of strings specifying one or more
   local log files to use. The files will store the information printed on
   stderr, along with the thread id and timestamp when the output has been
   generated.  
   For subkey *`"limit"`* the value is an integer setting the default for
   the log limit.  
   For subkey *`"plain"`* the value is a flag. If set, do not use ANSI escape
   sequences to highlight messages.  
   For subkey *`"append"`* the value is a flag. If set, append messages to log
   file instead of overwriting existing.

  - The value for the key *`"authentication"`* is a JSON object specifying
   client-side authentication options for **`just`** **`serve`** when
   communicating with the remote execution endpoint.  
   For subkey *`"ca cert"`* the value is a string specifying the path to a TLS
   CA certificate.  
   For subkey *`"client cert"`* the value is a string specifying the path to a
   TLS client certificate.  
   For subkey *`"client key"`* the value is a string specifying the path to a
   TLS client key.  

 - The value for the key *`"remote service"`* is a JSON object specifying the
   server arguments for running **`just`** **`serve`** as a service.  
   For subkey *`"interface"`* the value specifies the interface of the service.
   If unset, the loopback device is used.  
   For subkey *`"port"`* the value specifies the port to which the service is to
   listen. If unset, the service will choose to the first available one.  
   For subkey *`"pid file"`* the value specifies a file to which the pid should
   be stored in plain text. If the file exists, it will be overwritten.  
   For subkey *`"info file"`* the value specifies a file to which the used port,
   interface, and pid should be stored in JSON format. If the file exists, it
   will be overwritten.  
   For subkey *`"server cert"`* the value is a string specifying the path to a
   TLS server certificate.  
   For subkey *`"server key"`* the value is a string specifying the path to a
   TLS server key.  

 - The value for the key *`"execution endpoint"`* is a JSON object specifying
   the arguments of a remote execution endpoint to be used by **`just`**
   **`serve`**.  
   For subkey *`"address"`* the value is a string specifying the remote
   execution address in a NAME:PORT format.  
   For subkey *`"compatible"`* the value is a flag which specifies whether
   the remote endpoint uses the original remote execution protocol.  
   If the key *`"execution endpoint"`* is given, the following three keys will
   be evaluated as well:  
   - *`"max-attempts"`*: the value must be a number specifying the maximum
     number of attempts to perform when a remote procedure call (to the
     *`"execution endpoint"`* given) fails because the resource is unavailable.  
   - *`"initial-backoff-seconds"`*: the value must be a number; before retrying
     the second time, the client will wait the given amount of seconds plus a jitter,
     to better distribute the workload.  
   - *`"max-backoff-seconds"`*: the value must be a number; from the third
     attempt (included) on, the backoff time is doubled at each attempt, until
     it exceeds the `"max-backoff-seconds"` value. From that point, the waiting
     time is computed as `"max-backoff-seconds"` value plus a jitter.  

 - The value for the key *`"jobs"`* specifies the number of jobs to run. If
   unset, the number of available cores is used.  

 - The value for the key *`"build"`* is a JSON object specifying arguments used
   by **`just`** **`serve`** to orchestrate remote builds.  
   For subkey *`"build jobs"`* the value specifies the number of jobs to run
   during a remote build. If unset, the same value as for outer key *`"jobs"`*
   is used.  
   For subkey *`"action timeout"`* the value in a number specifying the timeout
   limit in seconds for actions run during a remote build. If unset, the default
   value 300 is used.  
   For subkey *`"target-cache write strategy"`* the value has to
   be one of the values *`"disable"`*, *`"sync"`*, or *`"split"`*.
   The default is *`"sync"`*, giving the instruction to
   synchronize artifacts and write target-level cache entries.
   The value *`"split"`* does the same using blob splitting
   when synchronizing artifacts, provided it is supported by the
   remote-execution endpoint. The value *`"disable"`* disables
   adding new entries to the target-level cache, which defeats the
   purpose of typical set up to share target-level computations
   between clients.  
   For the subkey *`"local launcher"`*, if given, the value has
   to be a list. This list is used as local launcher for the
   build in the case the serve process acts simultaneously as
   remote-execution endpoint. If unset (or `null`), the value
   `["env", "--"]` will be taken as default.

EXAMPLE
=======

An example serve configuration file could look as follows.

```jsonc
{ "local build root": "/var/just-serve/root"
, "authentication":
  { "ca cert": "/etc/just-serve/certs/ca.crt"
  , "client cert": "/etc/just-serve/certs/client.crt"
  , "client key": "/etc/just-serve/certs/client.key"
  }
, "remote service":
  { "interface": "192.0.2.1"
  , "port": 9999
  , "pid file": "/var/run/just-serve/server.pid"
  , "server cert": "/etc/just-serve/certs/server.crt"
  , "server key": "/etc/just-serve/certs/server.key"
  }
, "execution endpoint": {"address": "198.51.100.1"}
, "repositories":
  [ "/var/just-serve/repos/third-party-distfiles"
  , "/var/just-serve/repos/project-foo"
  , "/var/just-serve/repos/project-bar"
  ]
, "jobs": 8
, "build": {"build jobs": 128}
, "max-attempts": 10
, "initial-backoff-seconds": 10
, "max-backoff-seconds": 60
}
```

See also
========

**`just`**(1)
