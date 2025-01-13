% JUST(1) | General Commands Manual

NAME
====

just - a generic build tool

SYNOPSIS
========

**`just`** **`version`**  
**`just`** {**`analyse`**|**`build`**} \[*`OPTION`*\]... \[\[*`module`*\] *`target`*\]  
**`just`** **`install`** \[*`OPTION`*\]... **`-o`** *`OUTPUT_DIR`* \[\[*`module`*\] *`target`*\]  
**`just`** **`install-cas`** \[*`OPTION`*\]... *`OBJECT_ID`*  
**`just`** **`add-to-cas`** \[*`OPTION`*\]... *`PATH`*  
**`just`** **`describe`** \[*`OPTION`*\]... \[\[*`module`*\] *`target`*\]  
**`just`** **`rebuild`** \[*`OPTION`*\]... \[\[*`module`*\] *`target`*\]  
**`just`** **`traverse`** \[*`OPTION`*\]... **`-o`** *`OUTPUT_DIR`* **`-g`** *`GRAPH_FILE`*  
**`just`** **`gc`** \[*`OPTION`*\]...  
**`just`** **`execute`** \[*`OPTION`*\]...  
**`just`** **`serve`** *`SERVE_CONFIG_FILE`*

DESCRIPTION
===========

Just is a generic multi-repository build system; language-specific
knowledge is described in separate rule files. For every build action,
the relative location of the inputs is independent of their physical
location. This staging allows taking sources from different locations
(logical repositories), including bare Git repositories. Targets are
defined using JSON format, in proper files (by default, named
*`TARGETS`*). Targets are uniquely identified by their name, the
repository, and the module they belong to. A module is the relative path
from the repository target root directory to a subdirectory containing a
target file.

The module's name of the targets defined at the target root level is
the empty string. Specifying the correct repository, target root,
module, and target name allows to process that target independently of
the current working directory.

If the module is not specified on the command line, **`just`** sets the
module corresponding to the current working directory.

If a target is not specified, the lexicographically-first target,
according to native byte order, is used. So, a target named with an
empty string will always be the default target for that module.

If a target depends on other targets defined in other modules or
repositories, **`just`** will recursively visit all and only the required
modules.

The main repository is the repository containing the target specified on
the command line. The main repository can either be read from the
multi-repository configuration file if it contains the key *`"main"`* or
through the option **`--main`**. The command-line option **`--main`**
overrides what is eventually read from the multi-repository
configuration file. If neither the multi-repository configuration file
contains the *`"main"`* key nor the **`--main`** option is provided, the
lexicographical first repository from the multi-repository configuration
file is used as main.

The *`workspace_root`* of the main repository is then defined as
follows. If the option **`--workspace-root`** is provided, then
*`workspace_root`* is set accordingly. If the option is not provided,
**`just`** checks if it is specified within the multi-repository
configuration file. If it is, then it is set accordingly. If not, **`just`**
starts looking for a marker in the current directory first, then in all
the parent directories until it finds one. The supported markers are

 - *`ROOT`* file (can be empty, content is ignored)
 - *`WORKSPACE`* (can be empty, content is ignored)
 - *`.git`* (can be either a file - empty or not, content is ignored -
   or the famous directory)

If it fails, **`just`** errors out.

For non-main repositories, the *`workspace_root`* entry must be declared
in the multi-repository configuration file.

Afterwards, *`target_root`*, *`rule_root`*, and *`expression_root`*
directories for the main repository are set using the following
strategy. If the corresponding command-line option is specified, then it
is honored. Otherwise, it is read from the multi-repo configuration
file. If it is not specified there, the default value is used. The
default value of *`target_root`* is *`workspace_root`*, of *`rule_root`*
is *`target_root`*, and of *`expression_root`* is *`rule_root`*.

Finally, the file names where *targets*, *rules*, and *expressions* are
defined for the main repository. If the corresponding key is present in
the multi-repository configuration file, it is set accordingly. If the
user gives the corresponding command-line option, it overwrites what is
eventually read from the configuration file. If they are not explicitly
stated neither within the multi-repository configuration file nor on the
command line, the default values are used, which are *`TARGETS`*,
*`RULES`*, and *`EXPRESSIONS`*, respectively.

For non-main repositories (i.e., repositories defining targets required
by the target given on the command line), *`target_root`*,
*`rule_root`*, *`expression_root`*, and file names of targets, rules and
expressions, if different from the default values, must be declared in
the multi-repository configuration file.

SUBCOMMANDS
===========

**`version`**
-------------

Print on stdout a JSON object providing version information about the
version of the tool used. This JSON object will contain at least the
following keys.

 - *`"version"`* The version, as a list of numbers of length at least 3,
   following the usual convention that version numbers are compared
   lexicographically.
 - *`"suffix"`* The version suffix as a string. Generally, suffixes
   starting with a + symbol are positive offsets to the version, while
   suffixes starting with a *`~`* symbol are negative offsets.
 - *`"SOURCE_DATE_EPOCH"`* Either a number or *`null`*. If it is a
   number, it is the time, in seconds since the epoch, of the last
   commit that went into this binary. It is *`null`* if that time is not
   known (e.g., in development builds).

**`analyse`**|**`build`**|**`install`**
---------------------------------------

The subcommands **`analyse`**, **`build`**, and **`install`** are
strictly related. In fact, from left to right, one is a subset of the
other. **`build`** performs work on top of **`analyse`**, and
**`install`** on top of **`build`**. When a user issues **`build`**, the
**`analyse`** is called underneath. In particular, there is no need to
run these three subcommands sequentially.

### **`analyse`**

analyse reads the target graph from *`TARGETS`* files for the given
target, computes the action graph (required by e.g., **`build`**, **`install`**,
**`traverse`**), and reports the artifacts, provides, and runfiles of the
analysed target.

In short, the **`analyse`** subcommand identifies all the steps required
to **`build`** a given target without actually performing those steps.

This subcommand, issued with proper flags, can dump in JSON format
artifacts, action graph, nodes, actions, (transitive) targets (both
named and anonymous), and trees.

### **`build`**

This subcommand performs the actions contained in the action graph
computed through the **`analyse`** phase.

If building locally, the building process is performed in temporary
separate directories to allow for staging according to the logical path
described in the *`TARGETS`* file. Since artifacts are only stored in
the CAS, the user has to use either the **`install`** or **`install-cas`**
subcommand to get them.

**`just`** allows for both local (i.e., on the same machine where **`just`** is
used) and remote compilation (i.e., by sending requests over a TCP
connection, e.g., to a different machine, cluster or cloud
infrastructure). In case of a remote compilation, artifacts are compiled
remotely and stored in the remote CAS. **`install`** and **`install-cas`**
subcommands can be used to locally fetch and stage the desired
artifacts.

### **`install`**

The **`install`** subcommand determines which (if any) actions need to be
(re)done and issues the command to (re)run them. Then, it installs the
artifacts (stored in the local or remote CAS) of the processed target
under the given *`OUTPUT_DIR`* (set by option **`-o`**) honoring the
logical path (aka, staging). If the output path does not exist, it will
create all the necessary folders and subfolders. If files are already
present, they will be overwritten.

**`rebuild`**
-------------

This subcommand inspects if builds are fully reproducible or not (e.g.,
time stamps are used). It simply rebuilds and compares artifacts to the
cached build reporting actions with different output. To do so in a
meaningful way, it requires that previous build is already in the cache
(local or remote).

**`describe`**
--------------

The **`describe`** subcommand allows for describing the rule generating a
target. The rule is resolved in precisely the same way as during the
analysis. The doc-strings (if any) from the rule definition (if
user-defined) are reported, together with a summary of the declared
fields and their types. The multi-repository configuration is honored in
the same way as during **`analyse`** and **`build`**; in particular, the rule
definition can also reside in a git-tree root.

**`install-cas`**
-----------------

**`install-cas`** fetches artifacts from CAS (Content Addressable Storage) by
means of their *`OBJECT_ID`* (object identifier). The canonical format
of an object identifier is *`[<hash>:<size>:<type>]`*; however, when
parsing an object identifier, **`install-cas`** uses the following default
rules, to make usage simpler.

 - The square brackets are optional.
 - If the size is missing (e.g., because the argument contains no
   colon), or cannot be parsed as a number, this is not an error, and
   the value 0 is assumed. While this is almost never the correct size,
   many CAS implementations, including the local CAS of just itself,
   ignore the size for lookups.
 - From the type, only the first letter (*`f`* for non-executable file,
   *`x`* for executable file, and *`t`* for tree) is significant; the
   rest is ignored. If the type is missing (e.g., because the argument
   contains less than two colons), or its first letter is not one of the
   valid ones, *`f`* is assumed.

Depending on whether the output path is set or not, the behavior is
different.

### Output path is omitted

If the output path is omitted, it prints the artifact content to stdout
and if the artifact is a tree, it will print a human readable
description.

### Output path is set

1. Output path does not exist

   The artifact will be staged to that path. If artifact is a file, the
   installed one will have the name of the output path. If the artifact
   is a tree, it will create a directory named like the output path,
   and will stage all the entries (subtrees included) under that
   directory.

2. Output path exists and it is a directory

   If the artifact is a tree, a directory named with the hash of tree
   itself is created under the output path, and all the entries and
   subtrees are installed inside the hash-named directory.

   If the artifact is a file, it is installed under the output path and
   named according to the hash of the artifact itself.

3. Output path exists and it is a file

   If the artifact is a file, it will replace the existing file. If the
   artifact is a tree, it will cause an error.

**`add-to-cas`**
----------------

**`add-to-cas`** adds a file or directory to the local CAS and
reports the hash (without size or type information) on stdout. If a
remote endpoint is given, the object is also uploaded there. A main
use case of this command is to simplify the setup of `"git tree"`
repositories, where it can also avoid checking out a repository of
a foreign version-control system twice.


**`traverse`**
--------------

It allows for the building and staging of requested artifacts from a
well-defined *`GRAPH_FILE`*. See **`just-graph-file`**(5) for more
details.

**`gc`**
--------

The **`gc`** subcommand triggers garbage collection of the local cache.
More precisely, it rotates the cache and CAS generations. During a
build, upon cache hit, everything related to that cache hit is uplinked
to the youngest generation; therefore, upon a call to **`gc`** everything
not referenced since the last call to **`gc`** is purged and the
corresponding disk space reclaimed.

Additionally, and before doing generation rotation,
 - left-over temporary directories (e.g., from interrupted `just`
   invocations) are removed, and
 - large files are split and only the chunks and the information
   how to assemble the file from the chunks are kept; in this way
   disk space is saved without losing information.

As the non-rotating tasks can be useful in their own right, the
`--no-rotate` option can be used to request only the clean-up tasks
that do not lose information.

**`execute`**
-------------

This subcommand starts a single node remote execution service, honoring
the just native remote protocol.

If the flag **`--compatible`** is provided, the execution service will
honor the original remote build execution protocol.

**`serve`**
-----------

This subcommand starts a service that provides target dependencies needed for a
remote execution build. It expects as its only and mandatory argument the path
to a configuration file, following the format described in
**`just-serve-config`**(5).

OPTIONS
=======

Generic program information
---------------------------

**`-h`**, **`--help`**  
Output a usage message and exit.  
Supported by: all subcommands.

Compatibility options
---------------------

**`--compatible`**  
At increased computational effort, be compatible with the original
remote build execution protocol. As the change affects identifiers, the
flag must be used consistently for all related invocations.  
Supported by:
add-to-cas|analyse|build|describe|install-cas|install|rebuild|traverse|execute.

Build configuration options
---------------------------

**`--action-timeout`** *`NUM`*  
Action timeout in seconds. (Default: 300). The timeout is honored only
for the remote build.  
Supported by: build|install|rebuild|traverse.

**`-c`**, **`--config`** *`PATH`*  
Path to configuration file.  
Supported by: analyse|build|describe|install|rebuild.

**`-C`**, **`--repository-config`** *`PATH`*  
Path to configuration file for multi-repository builds. See
**`just-repository-config`**(5) for more details.  
Supported by: analyse|build|describe|install|rebuild|traverse.

**`-D`**, **`--defines`** *`JSON`*  
Defines, via an in-line JSON object a configuration to overlay (in the
sense of *`map_union`*) to the configuration obtained by the
**`--config`** option. If **`-D`** is given several times, the **`-D`**
options overlay in the order they are given on the command line.  
Supported by: analyse|build|describe|install|rebuild.

**`--request-action-input`** *`ACTION`*  
Modify the request to be, instead of the analysis result of the
requested target, the input stage of the specified action as artifacts,
with empty runfiles and a provides map providing the remaining
information about the action, in particular as *`"cmd"`* the arguments
vector and *`"env"`* the environment.

An action can be specified in the following ways

 - an action identifier prefixed by the *`%`* character
 - a number prefixed by the *`#`* character (note that it requires
   quoting on most shells). This specifies the action with that index of
   the actions associated directly with that target; the indices start
   from 0 onwards, and negative indices count from the end of the array
   of actions.
 - an action identifier or number without prefix, provided the action
   identifier does not start with either *`%`* or *`#`* and the number
   does not happen to be a valid action identifier.

Supported by: analyse|build|describe|install|rebuild.

**`--expression-file-name`** *`TEXT`*  
Name of the expressions file.  
Supported by: analyse|build|describe|install|rebuild.

**`--expression-root`** *`PATH`*  
Path of the expression files' root directory. Default: Same as
**`--rule-root`**.  
Supported by: analyse|build|describe|install|rebuild.

**`-L`**, **`--local-launcher`** *`JSON_ARRAY`*  
JSON array with the list of strings representing the launcher to prepend
actions' commands before being executed locally. Default value:
*`["env", "--"]`*  
Supported by: build|install|rebuild|traverse|execute.

**`--local-build-root`** *`PATH`*  
Root for local CAS, cache, and build directories. The path will be
created if it does not exist already.  
Supported by: add-to-cas|build|describe|install-cas|install|rebuild|traverse|gc|execute.

**`--main`** *`NAME`*  
The repository to take the target from.  
Supported by: analyse|build|describe|install|rebuild|traverse.

**`--rule-file-name`** *`TEXT`*  
Name of the rules file.  
Supported by: analyse|build|describe|install|rebuild.

**`--rule-root`** *`PATH`*  
Path of the rule files' root directory. Default: Same as
**`--target-root`**  
Supported by: analyse|build|describe|install|rebuild.

**`--target-file-name`** *`TEXT`*  
Name of the targets file.  
Supported by: analyse|build|describe|install|rebuild.

**`--target-root`** *`PATH`*  
Path of the target files' root directory. Default: Same as
**`--workspace-root`**  
Supported by: analyse|build|describe|install|rebuild.

**`-w`**, **`--workspace-root`** *`PATH`*  
Path of the workspace's root directory.  
Supported by: analyse|build|describe|install|rebuild|traverse.

General output options
----------------------

**`--dump-artifacts-to-build`** *`PATH`*  
File path for writing the artifacts to build to. Output format is JSON
map with staging path as key, and intentional artifact description as
value.  
Supported by: analyse|build|install|rebuild.

**`--dump-artifacts`** *`PATH`*  
Dump artifacts generated by the given target. Using *`-`* as PATH, it is
interpreted as stdout. Note that, passing *`.`*/*`-`* will instead
create a file named *`-`* in the current directory. Output format is
JSON map with staging path as key, and object id description (hash,
type, size) as value. Each artifact is guaranteed to be *`KNOWN`* in
CAS. Therefore, this option cannot be used with **`analyse`**.  
Supported by: build|install|rebuild|traverse.

**`--dump-graph`** *`PATH`*  
File path for writing the action graph description to. See
**`just-graph-file`**(5) for more details.  
Supported by: analyse|build|install|rebuild.

**`--dump-plain-graph`** *`PATH`*  
File path for writing the action graph description to, however without
the additional `"origins"` key. See **`just-graph-file`**(5) for more details.  
Supported by: analyse|build|install|rebuild.

**`-f`**, **`--log-file`** *`PATH`*  
Path to local log file. **`just`** will store the information printed on
stderr in the log file along with the thread id and timestamp when the
output has been generated.  
Supported by:
add-to-cas|analyse|build|describe|install|install-cas|rebuild|traverse|gc|execute.

**`--log-limit`** *`NUM`*  
Log limit (higher is more verbose) in interval \[0,6\] (Default: 3).  
Supported by:
add-to-cas|analyse|build|describe|install|install-cas|rebuild|traverse|gc|execute.

**`--restrict-stderr-log-limit`** *`NUM`*  
Restrict logging on console to the minimum of the specified **`--log-limit`**
and the value specified in this option. The default is to not additionally
restrict the log level at the console.  
Supported by:
add-to-cas|analyse|build|describe|install|install-cas|rebuild|traverse|gc|execute.

**`--plain-log`**  
Do not use ANSI escape sequences to highlight messages.  
Supported by:
add-to-cas|analyse|build|describe|install|install-cas|rebuild|traverse|gc|execute.

**`--log-append`**  
Append messages to log file instead of overwriting existing.  
Supported by:
add-to-cas|analyse|build|describe|install|install-cas|rebuild|traverse|gc|execute.

**`--expression-log-limit`** *`NUM`*  
In error messages, truncate the entries in the enumeration of the active
environment, as well as the expression to be evaluated, to the specified
number of characters (default: 320).  
Supported by: analyse|build|install.

**`--serve-errors-log`** *`PATH`*  
Path to local file in which **`just`** will write, in machine
readable form, the references to all errors that occurred on the
serve side. More precisely, the value will be a JSON array with one
element per failure, where the element is a pair (array of length
2) consisting of the configured target (serialized, as usual, as a
pair of qualified target name an configuration) and a string with
the hex representation of the blob identifier of the log; the log
itself is guaranteed to be available on the remote-execution side.  
Supported by: analyse|build|install.

**`-P`**, **`--print-to-stdout`** *`LOGICAL_PATH`*  
After building, print the specified artifact to stdout.  
Supported by: build|install|rebuild|traverse.

**`-s`**, **`--show-runfiles`**  
Do not omit runfiles in build report.  
Supported by: build|install|rebuild|traverse.

**`--target-cache-write-strategy`** *`STRATEGY`*  
Strategy for creating target-level cache entries. Supported values are

 - *`sync`* Synchronize the artifacts of the export targets and write
   target-level cache entries. This is the default behaviour.
 - *`split`* Synchronize the artifacts of the export targets, using
   blob splitting if the remote-execution endpoint supports it,
   and write target-level cache entries. As opposed to the default
   strategy, additional entries (the chunks) are created in the CAS,
   but subsequent syncs of similar blobs might need less traffic.
 - *`disable`* Do not write any target-level cache entries. As
   no artifacts have to be synced, this can be useful for one-off
   builds of a project or when the connection to the remote-execution
   endpoint is behind a very slow network.

Supported by: build|install|rebuild.


Output dir and path
-------------------

**`-o`**, **`--output-dir`** *`PATH`*  
Path of the directory where outputs will be copied. If the output path
does not exist, it will create all the necessary folders and subfolders.
If the artifacts have been already staged, they will be overwritten.  
Required by: install|traverse.

**`-o`**, **`--output-path`** *`PATH`*  
Install path for the artifact. Refer to **`install-cas`** section for more
details.  
Supported by: install-cas.

**`--archive`**  
Instead of installing the requested tree, install an archive with the
content of the tree. It is a user error to specify **`--archive`** and
not request a tree.  
Supported by: install-cas.

**`--raw-tree`**  
When installing a tree to stdout, i.e., when no option **`-o`** is given,
dump the raw tree rather than a pretty-printed version. This option is
ignored if **`--archive`** is given.  
Supported by: install-cas.

**`-P`**, **`--sub-object-path`** *`PATH`*  
Instead of the specified tree object take the object at the specified
logical path inside.  
Supported by: install-cas.

**`--remember`**  
Ensure that all installed artifacts are available in local CAS as well,
even when using remote execution.  
Supported by: install|traverse|install-cas.

Parallelism options
-------------------

**`-J`**, **`--build-jobs`** *`NUM`*  
Number of jobs to run during build phase. Default: same as **`--jobs`**.  
Supported by: build|install|rebuild|traverse.

**`-j`**, **`--jobs`** *`NUM`*  
Number of jobs to run. Default: Number of cores.  
Supported by: analyse|build|describe|install|rebuild|traverse.

Remote execution options
------------------------

As remote execution properties shard the target-level cache, they are
also available for analysis. In this way, the same action identifiers
can be achieved despite the extensional projection inherent to target
level caching, e.g., in conjunction with **`--request-action-input`**.

**`--remote-execution-property`** *`KEY`*:*`VAL`*  
Property for remote execution as key-value pair. Specifying this option
multiple times will accumulate pairs. If multiple pairs with the same
key are given, the latest wins.  
Supported by: analyse|build|install|rebuild|traverse.

**`-r`**, **`--remote-execution-address`** *`NAME`*:*`PORT`*  
Address of the remote execution service.  
Supported by: add-to-cas|analyse|build|describe|install-cas|install|rebuild|traverse.

**`--endpoint-configuration`** FILE  
File containing a description on how to dispatch to different
remote-execution endpoints based on the execution properties.
The format is a JSON list of pairs (lists of length two) of an object
of strings and a string. The first entry describes a condition (the
remote-execution properties have to agree on the domain of this
object), the second entry is a remote-execution address in the NAME:PORT
format as for the **`-r`** option. The first matching entry (if any) is taken;
if none matches, the default execution endpoint is taken (either
as specified by **`-r`**, or local execution if no endpoint is
specified).  
Supported by: analyse|build|install|rebuild|traverse.

**`--max-attempts`** *`NUM`*  
If a remote procedure call (rpc) returns `grpc::StatusCode::UNAVAILABLE`, that
rpc is retried at most *`NUM`* times. (Default: 1, i.e., no retry).  
Supported by: analyse|build|describe|install|rebuild|traverse.

**`--initial-backoff-seconds`** *`NUM`*  
Before retrying the second time, the client will wait the given amount of
seconds plus a jitter, to better distribute the workload. (Default: 1).  
Supported by: analyse|build|describe|install|rebuild|traverse.

**`--max-backoff-seconds`** *`NUM`*  
From the third attempt (included) on, the backoff time is doubled at
each attempt, until it exceeds the `max-backoff-seconds`
parameter. From that point, the waiting time is computed as
`max-backoff-seconds` plus a jitter. (Default: 60)  
Supported by: analyse|build|describe|install|rebuild|traverse.

Remote serve options
--------------------

**`-R`**, **`--remote-serve-address`** *`NAME`*:*`PORT`*  
Address of the remote execution service.  
Supported by: analyse|build|describe|install|rebuild.

Authentication options
----------------------

Only TLS and mutual TLS (mTLS) are supported.

**`--tls-ca-cert`** *`PATH`*  
Path to a TLS CA certificate that is trusted to sign the server
certificate.  
Supported by: add-to-cas|analyse|build|describe|install-cas|install|rebuild|traverse|execute.

**`--tls-client-cert`** *`PATH`*  
Path to a TLS client certificate to enable mTLS. It must be passed in
conjunction with **`--tls-client-key`** and **`--tls-ca-cert`**.  
Supported by: add-to-cas|analyse|build|describe|install-cas|install|rebuild|traverse.

**`--tls-client-key`** *`PATH`*  
Path to a TLS client key to enable mTLS. It must be passed in
conjunction with **`--tls-client-cert`** and **`--tls-ca-cert`**.  
Supported by: add-to-cas|analyse|build|describe|install-cas|install|rebuild|traverse.

**`analyse`** specific options
------------------------------

**`--dump-actions`** *`PATH`*  
Dump actions to file. *`-`* is treated as stdout. Output is a list of
action descriptions, in JSON format, for the given target.

**`--dump-anonymous`** *`PATH`*  
Dump anonymous targets to file. *`-`* is treated as stdout. Output is a
JSON map, for all transitive targets, with two entries: *`nodes`* and
*`rule_maps`*. The former contains maps between node id and the node
description. *`rule_maps`* states the maps between the *`mode_type`* and
the rule to use in order to make a target out of the node.

**`--dump-blobs`** *`PATH`*  
Dump blobs to file. *`-`* is treated as stdout. The term *`blob`*
identifies a collection of strings that the execution back end should be
aware of before traversing the action graph. A blob, will be referred to
as a *`KNOWN`* artifact in the action graph.

**`--dump-nodes`** *`PATH`*  
Dump nodes of only the given target to file. *`-`* is treated as stdout.
Output is a JSON map between node id and its description.

**`--dump-vars`** *`PATH`*  
Dump configuration variables to file. *`-`* is treated as stdout. The
output is a JSON list of those variable names (in lexicographic order)
at which the configuration influenced the analysis of this target. This
might contain variables unset in the configuration if the fact that they
were unset (and hence treated as the default *`null`*) was relevant for
the analysis of that target.

**`--dump-targets`** *`PATH`*  
Dump all transitive targets to file for the given target. *`-`* is
treated as stdout. Output is a JSON map of all targets encoded as tree
by their entity name:

``` jsonc
{ "#": // anonymous targets
  { "<rule_map_id>":
    { "<node_id>": ["<serialized config1>", ...] } // all configs this target is configured with
  }
, "@": // "normal" targets
  { "<repo>":
    { "<module>":
      { "<target>": ["<serialized config1>", ...] } // all configs this target is configured with
    }
  }
}
```

**`--dump-export-targets`** *`PATH`*  
Dump all transitive targets to file for the given target that are export
targets. *`-`* is treated as stdout. The output format is the same as
for **`--dump-targets`**.

**`--dump-targets-graph`** *`PATH`*  
Dump the graph of configured targets to a file (even if it is called
*`-`*). In this graph, only non-source targets are reported. The graph
is represented as a JSON object. The keys are the nodes of the graph,
and for each node, the value is a JSON object containing the different
kind of dependencies (each represented as a list of nodes).

 - *`"declared"`* are the dependencies coming from the target fields in
   the definition of the target
 - *`"implicit"`* are the dependencies implicit from the rule definition
 - *`"anonymous"`* are the dependencies on anonymous targets implicitly
   referenced during the evaluation of that rule

While the node names are strings (so that they can be keys in a JSON
object), they can themselves be decoded as JSON and in this way
precisely name the configured target. More precisely, the JSON decoding
of a node name is a list of length two, with the first entry being the
target name (as *`["@", repo, module, target]`* or _`["#", rule_map_id,
node_id]`_) and the second entry the effective configuration.

**`--dump-trees`** *`PATH`*  
Dump trees and all subtrees of the given target to file. *`-`* is
treated as stdout. Output is a JSON map between tree ids and the
corresponding artifact map, which maps the path to the artifact
description.

**`--dump-provides`** *`PATH`*  
Dump the provides map of the given target to file. *`-`* is treated
as stdout. The output is a JSON object mapping the providers to their
values, serialized as JSON; in particular, artifacts are replaced
by a JSON object with their intensional description. Therefore, the
dumped JSON is not uniquely readable, but requires an out-of-band
understanding where artifacts are to be expected.

**`--dump-result`** *`PATH`*  
Dump the result of the analysis for the requested target to
file. *`-`* is treated as stdout. The output is a JSON object with the
keys *`"artifacts"`*, *`"provides"`*, and *`"runfiles"`*.

**`rebuild`** specific options
------------------------------

**`--vs`** *`NAME`*:*`PORT`*|*`"local"`*  
Cache endpoint to compare against (use *`"local"`* for local cache).

**`--dump-flaky`** *`PATH`*  
Dump flaky actions to file.

**`add-to-cas`** specific options
---------------------------------

**`--follow-symlinks`**  
Resolve the positional argument to not be a symbolic link by following
symbolic links. The default is to add the link itself, i.e., the string
obtained by **`readlink`**(2), as blob.

**`traverse`** specific options
-------------------------------

**`-a`**, **`--artifacts`** *`TEXT`*  
JSON maps between relative path where to copy the artifact and its
description (as JSON object as well).

**`-g`**, **`--graph-file`** *`TEXT`* *`[[REQUIRED]]`*  
Path of the file containing the description of the actions. See
**`just-graph-file`**(5) for more details.

**`--git-cas`** *`TEXT`*  
Path to a Git repository, containing blobs of potentially missing
*`KNOWN`* artifacts.

**`describe`** specific options
-------------------------------

**`--json`**  
Omit pretty-printing and describe rule in JSON format.

**`--rule`**  
Module and target arguments refer to a rule instead of a target.

**`execute`** specific options
------------------------------

**`-p`**, **`--port`** *`INT`*  
Execution service will listen to this port. If unset, the service will
listen to the first available one.

**`--info-file`** *`TEXT`*  
Write the used port, interface, and pid to this file in JSON format. If
the file exists, it will be overwritten.

**`-i`**, **`--interface`** *`TEXT`*  
Interface to use. If unset, the loopback device is used.

**`--pid-file`** *`TEXT`*  
Write pid to this file in plain txt. If the file exists, it will be
overwritten.

**`--tls-server-cert`** *`TEXT`*  
Path to the TLS server certificate.

**`--tls-server-key`** *`TEXT`*  
Path to the TLS server key.

**`--log-operations-threshold`** *`INT`*  
Once the number of operations stored exceeds twice *`2^n`*, where *`n`*
is given by the option **`--log-operations-threshold`**, at most *`2^n`*
operations will be removed, in a FIFO scheme. If unset, defaults to
14. Must be in the range \[0,63\].

**`gc`** specific options
-------------------------

**`--no-rotate`**  
Do not rotate gargabe-collection generations. Instead, only carry
out clean up tasks that do not affect what is stored in the cache.


EXIT STATUS
===========

The exit status of **`just`** is one of the following values:

 - 0: the command completed successfully
 - 1: the command could not complete due to some errors (e.g.,
   compilation errors, missing arguments, syntax errors, etc.)
 - 2: the command successfully parsed all the needed files (e.g.,
   *`TARGETS`*), successfully compiled the eventually required objects,
   but the generation of some artifacts failed (e.g., a test failed).

See also
========

**`just-repository-config`**(5),
**`just-serve-config`**(5),
**`just-mr`**(1)
