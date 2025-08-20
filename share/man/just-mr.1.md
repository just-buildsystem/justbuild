% JUST-MR(1) | General Commands Manual

NAME
====

just-mr - multi-repository configuration tool and launcher for the build tool

SYNOPSIS
========

**`just-mr`** \[*`OPTION`*\]... **`mrversion`**  
**`just-mr`** \[*`OPTION`*\]... {**`setup`**|**`setup-env`**} \[**`--all`**\] \[*`main-repo`*\]  
**`just-mr`** \[*`OPTION`*\]... **`fetch`** \[**`--all`**\] \[**`--backup-to-remote`**] \[**`-o`** *`fetch-dir`*\] \[*`main-repo`*\]  
**`just-mr`** \[*`OPTION`*\]... **`update`** \[*`repo`*\]...  
**`just-mr`** \[*`OPTION`*\]... **`gc-repo`** \[**`--drop-only`**\]  
**`just-mr`** \[*`OPTION`*\]... **`do`** \[*`JUST_ARG`*\]...  
**`just-mr`** \[*`OPTION`*\]... {**`version`**|**`describe`**|**`analyse`**|**`build`**|**`install`**|**`install-cas`**|**`add-to-cas`**|**`rebuild`**|**`gc`**} \[*`JUST_ARG`*\]...  

DESCRIPTION
===========

Just-MR is a configuration tool for the multi-repository Just build
system. It can be used both standalone and as a launcher for
**`just`**(1).

The tool performs specific operations, based on the invoked subcommand,
on repositories described in a configuration file. All subcommands
operate at the level of *workspace roots* deduced from the given
repository descriptions. See **`just-mr-repository-config`**(5) for more
details on the input format.

OPTIONS
=======

General options
---------------

**`-h`**, **`--help`**  
Output a usage message and exit.

**`-C`**, **`--repository-config`** *`PATH`*  
Path to the multi-repository configuration file. See
**`just-mr-repository-config`**(5) for more details. If no configuration
file is specified, **`just-mr`** will look for one in the following
order:

 - *`$WORKSPACE_ROOT/repos.json`* (workspace of the **`just-mr`** invocation)
 - *`$WORKSPACE_ROOT/etc/repos.json`* (workspace of the **`just-mr`**
   invocation)
 - *`$HOME/.just-repos.json`*
 - *`/etc/just-repos.json`*

The default configuration lookup order can be adjusted in the just-mrrc
file. See **`just-mrrc`**(5) for more details.

**`--absent`** *`PATH`*  
Path to a file specifying which repositories are to be considered
absent, overriding the values set by the *`"pragma"`* entries in the
multi-repository configuration. The file has to contain a JSON array
of those repository names to be considered absent.

**`-D`**, **`--defines`** *`JSON`*  
Defines, via an in-line JSON object, an overlay configuration for
**`just`**(1); if used as a launcher for a subcommand known to support
**`--defines`**, this defines value is forwarded, otherwise it is
ignored. If **`-D`** is given several times, the **`-D`** options
overlay (in the sense of *`map_union`*) in the order they are given on
the command line.

**`--local-build-root`** *`PATH`*  
Root for local CAS, cache, and build directories. The path will be
created if it does not exist already. This option overwrites any values
set in the **`just-mrrc`**(5) file.  
Default: path *`".cache/just"`* in user's home directory.

**`--checkout-locations`** *`PATH`*  
Specification file for checkout locations and additional mirrors.
This file contains a JSON object with several known keys:

 - the key *`"<version control>"`* of key *`"checkouts"`* specifies
   pairs of repository URLs as keys and absolute paths as values.
   Currently supported version control is Git, therefore
   the respective key is *`"git"`*. The paths contained for each repository
   URL point to existing locations on the filesystem containing the
   checkout of the respective repository.  
 - the key *`"local mirrors"`*, if given, is a JSON object mapping primary
   URLs to a list of local (non-public) mirrors. These mirrors are always
   tried first (in the given order) before any other URL is contacted.
 - the key *`"preferred hostnames"`*, if given, is a list of strings
   specifying known hostnames. When fetching from a non-local mirror, URLs
   with hostnames in the given list are preferred (in the order given)
   over URLs with other hostnames.
 - the key *`"extra inherit env"`*, if given, is a list of strings
   specifying additional variable names to be inherited from the
   environment (besides the ones specified in *`"inherit env"`*
   of the respective repository definition). This can be useful,
   if the local git mirrors use a different protocol (like `ssh`
   instead of `https`) and hence require different variables to
   pass the credentials.

This options overwrites any values set in the **`just-mrrc`**(5) file.  
Default: file path *`".just-local.json"`* in user's home directory.

**`-L`**, **`--local-launcher`** *`JSON_ARRAY`*  
JSON array with the list of strings representing the launcher to prepend
actions' commands before being executed locally.  
Default: *`["env", "--"]`*.

**`--distdir`** *`PATH`*  
Directory to look for distfiles before fetching. If given, this will be
the first place distfiles are looked for. This option can be given
multiple times to specify a list of distribution directories that are
used for lookup in the order they appear on the command line.
Directories specified via this option will be appended to the ones set
in the **`just-mrrc`**(5) file.  
Default: the single file path *`".distfiles"`* in user's home directory.

**`--main`** *`NAME`*  
The repository to take the target from.

**`-f`**, **`--log-file`** *`PATH`*  
Path to local log file. **`just-mr`** will store the information printed on
stderr in the log file along with the thread id and timestamp when the
output has been generated.

**`--log-limit`** *`NUM`*  
Log limit (higher is more verbose) in interval \[0,6\] (Default: 3).

**`--restrict-stderr-log-limit`** *`NUM`*  
Restrict logging on console to the minimum of the specified **`--log-limit`**
and the value specified in this option. The default is to not additionally
restrict the log level at the console.

**`--plain-log`**  
Do not use ANSI escape sequences to highlight messages.

**`--log-append`**  
Append messages to log file instead of overwriting existing.

**`--no-fetch-ssl-verify`**  
Disable the default peer SSL certificate verification step when fetching
archives (for which we verify the hash anyway) from remote.

**`--fetch-cacert`** *`PATH`*  
Path to the CA certificate bundle containing one or more certificates to
be used to peer verify archive fetches from remote.

**`-r`**, **`--remote-execution-address`** *`NAME`*:*`PORT`*  
Address of a remote execution service. This is used as an intermediary fetch
location for archives, between local CAS (or distdirs) and the network.

**`-R`**, **`--remote-serve-address`** *`NAME`*:*`PORT`*  
Address of a **`just`** **`serve`** service. This is used as intermediary fetch
location for Git commits, between local CAS and the network.

**`--max-attempts`** *`NUM`*  
If a remote procedure call (rpc) returns `grpc::StatusCode::UNAVAILABLE`, that
rpc is retried at most *`NUM`* times. (Default: 1, i.e., no retry).

**`--initial-backoff-seconds`** *`NUM`*  
Before retrying the second time, the client will wait the given amount of
seconds plus a jitter, to better distribute the workload. (Default: 1).

**`--max-backoff-seconds`** *`NUM`*  
From the third attempt (included) on, the backoff time is doubled at
each attempt, until it exceeds the `max-backoff-seconds`
parameter. From that point, the waiting time is computed as
`max-backoff-seconds` plus a jitter. (Default: 60)

**`--fetch-absent`**  
Try to make available all repositories, including those marked as absent.
This option cannot be set together with **`--compatible`**.

**`--compatible`**  
At increased computational effort, be compatible with the original remote build
execution protocol. If a remote execution service address is provided, this 
option can be used to match the artifacts expected by the remote endpoint.

**`--just`** *`PATH`*  
Name of the just binary in *`PATH`* or path to the just binary.  
Default: *`"just"`*.

**`--rc`** *`PATH`*  
Path to the just-mrrc file to use. See **`just-mrrc`**(5) for more
details.  
Default: file path *`".just-mrrc"`* in the user's home directory.

**`--dump-rc`** *`PATH`*  
Dump the effective rc, i.e., the rc after overlaying all applicable auxiliary
files specified in the `"rc files"` field, to the specified file. In this
way, an rc can be made self-contained in preparation for committing it to
a repository.

**`--git`** *`PATH`*  
Path to the git binary in *`PATH`* or path to the git binary. Used in
the rare instances when shelling out to git is needed.  
Default: *`"git"`*.

**`--norc`**  
Option to prevent reading any **`just-mrrc`**(5) file.

**`-j`**, **`--jobs`** *`NUM`*  
Number of jobs to run.  
Default: Number of cores.  

Authentication options
----------------------

Only TLS and mutual TLS (mTLS) are supported.
They mirror the **`just`**(1) options.

**`--tls-ca-cert`** *`PATH`*  
Path to a TLS CA certificate that is trusted to sign the server
certificate.

**`--tls-client-cert`** *`PATH`*  
Path to a TLS client certificate to enable mTLS. It must be passed in
conjunction with **`--tls-client-key`** and **`--tls-ca-cert`**.

**`--tls-client-key`** *`PATH`*  
Path to a TLS client key to enable mTLS. It must be passed in
conjunction with **`--tls-client-cert`** and **`--tls-ca-cert`**.

SUBCOMMANDS
===========

**`mrversion`**
---------------

Print on stdout a JSON object providing version information for this
tool itself; the **`version`** subcommand calls the **`version`** subcommand of
just. The version information for just-mr is in the same format that
also **`just`** uses.

**`setup`**|**`setup-env`**
---------------------------

These subcommands fetch all required repositories and generate an
appropriate multi-repository **`just`** configuration file. The resulting
file is stored in CAS and its path is printed to stdout. See
**`just-repository-config`**(5) for more details on the resulting
configuration file format.

If a main repository is provided in the input configuration or on
command line, only it and its dependencies are considered in the
generation of the resulting multi-repository configuration file. If no
main repository is provided, the lexicographical first repository from
the configuration is used. To perform the setup for all repositories
from the input configuration file, use the **`--all`** flag.

The behavior of the two subcommands differs only with respect to the
main repository. In the case of **`setup-env`**, the workspace root of the
main repository is left out, such that it can be deduced from the
working directory when **`just`** is invoked. In this way, working on a
checkout of that repository is possible, while having all of its
dependencies properly set up. In the case of **`setup`**, the workspace root
of the main repository is taken as-is into the output configuration
file.

fetch
-----

This subcommand prepares all archive-type and **`"git tree"`** workspace roots
for an offline build by fetching all their required source files from the
specified locations given in the input configuration file or ensuring the 
specified tree is present in the Git cache, respectively. Any subsequent
**`just-mr`** or **`just`** invocations containing fetched archive or 
**`"git tree"`** workspace roots will thus need no further network connections.

If a main repository is provided in the input configuration or on
command line, only it and its dependencies are considered for fetching.
If no main repository is provided, the lexicographical first repository
from the configuration is used. To perform the fetch for all
repositories from the input configuration file, use the **`--all`**
flag.

By default the first existing distribution directory is used as the
output directory for writing the fetched archives on disk. If no
existing distribution directory can be found an error is produced. To
define an output directory that is independent of the given distribution
directories, use the **`-o`** option.

Additionally, and only in *native mode*, the **`--backup-to-remote`** option can
be used in combination with the **`--remote-execution-address`** argument to
synchronize the locally fetched archives, as well as the **`"git tree"`** 
workspace roots, with a remote endpoint.

update
------

This subcommand updates the specified repositories (possibly none) and
prints the resulting updated configuration file to stdout.

Currently, **`just-mr`** can only update Git repositories and it will fail
if a different repository type is given. The tool also fails if any of
the given repository names are not found in the configuration file.

For Git repositories, the subcommand will replace the value for the
*`"commit"`* field with the commit hash (as a string) found in the
remote repository in the specified branch. The output configuration file
will otherwise remain the same at the JSON level with the input
configuration file.

gc-repo
-------

This subcommand rotates the generations of the repository cache.
Every root used is added to the youngest generation. Therefore upon
a call to **`gc-repo`** all roots are cleaned up that were not used
since the last **`gc-repo`**.

If **`--drop-only`** is given, only the old generations are cleaned up,
without rotation. In this way, storage can be reclaimed; this might be
necessary as no perfect sharing happens between the repository generations.

do
--

This subcommand is used as the canonical way of specifying just
arguments and calling **`just`** via **`execvp`**(2). Any subsequent argument
is unconditionally forwarded to **`just`**. For *known* subcommands
(**`version`**, **`describe`**, **`analyse`**, **`build`**, **`install`**, 
**`install-cas`**, **`add-to-cas`**, **`rebuild`**, **`gc`**), the
**`just-mr setup`** step is performed first for those commands accepting a
configuration and the produced configuration is prefixed to the provided
arguments. The main repository for the **`setup`** step can be provided in
the configuration or on the command line. If no main repository is
provided, the lexicographical first repository from the configuration is
used.

All logging arguments given to **`just-mr`** are passed to **`just`** as early
arguments. If log files are provided, an unconditional
**`--log-append`** argument is passed as well, which ensures no log
messages will get overwritten.

The **`--local-launcher`** argument is passed to **`just`** as early
argument for those *known* subcommands that accept it (build, install,
rebuild).

The **`--remote-execution-address`**, **`--compatible`**, and 
**`--remote-serve-address`** arguments are passed to **`just`** as early
arguments for those *known* subcommands that accept them
(analyse, build, install-cas, add-to-cas, install, rebuild, traverse).

The *authentication options* given to **`just-mr`** are passed to **`just`** as
early arguments for those *known* subcommands that accept them, according to
**`just`**(1).

**`version`**|**`describe`**|**`analyse`**|**`build`**|**`install`**|**`install-cas`**|**`add-to-cas`**|**`rebuild`**|**`gc`**
------------------------------------------------------------------------------------------------------------------------------

This subcommand is the explicit way of specifying *known* just
subcommands and calling **`just`** via **`execvp`**(2). The same description
as for the **`do`** subcommand applies.

**`gc-repo`**
-------------

Rotate the repository-root generations. In this way, all repository
roots not needed since the the last call to **`gc-repo`** are purged
and the corresponding disk space reclaimed.


EXIT STATUS
===========

The exit status of **`just-mr`** is one of the following values:

 - 0: the command completed successfully
 - 64: setup succeeded, but exec failed
 - 65: any unspecified error occurred in just-mr
 - 66: unknown subcommand (internal implementation error of **`just-mr`**)
 - 67: error parsing the command-line arguments
 - 68: error parsing the configuration
 - 69: error during fetch
 - 70: error during update
 - 71: error during setup

Any other exit code that does not have bit 64 set is a status value from
**`just`**, if **`just-mr`** is used as a launcher. See **`just`**(1) for more
details.

See also
========

**`just-mrrc`**(5),
**`just-mr-repository-config`**(5),
**`just-repository-config`**(5),
**`just`**(1)
