% JUST-MR(1) | General Commands Manual

NAME
====

just-mr - multi-repository configuration tool and launcher for
**`just`**(1)

SYNOPSIS
========

**`just-mr`** \[*`OPTION`*\]... **`mrversion`**  
**`just-mr`** \[*`OPTION`*\]... {**`setup`**|**`setup-env`**} \[**`--all`**\] \[*`main-repo`*\]  
**`just-mr`** \[*`OPTION`*\]... **`fetch`** \[**`--all`**\] \[**`-o`** *`fetch-dir`*\] \[*`main-repo`*\]  
**`just-mr`** \[*`OPTION`*\]... **`update`** \[*`repo`*\]...  
**`just-mr`** \[*`OPTION`*\]... **`do`** \[*`JUST_ARG`*\]...  
**`just-mr`** \[*`OPTION`*\]... {**`version`**|**`analyse`**|**`build`**|**`install`**|**`install-cas`**|**`describe`**|**`rebuild`**} \[*`JUST_ARG`*\]...  

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
Specification file for checkout locations. This file contains a JSON
object, for which under the key *`"<version control>"`* of key
*`"checkouts"`* we get pairs of repository URLs as keys and absolute
paths as values. Currently supported version control is Git, therefore
the respective key is *`"git"`*. The paths contained for each repository
URL point to existing locations on the filesystem containing the
checkout of the respective repository. This options overwrites any
values set in the **`just-mrrc`**(5) file.  
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

**`--just`** *`PATH`*  
Name of the just binary in *`PATH`* or path to the just binary.  
Default: *`"just"`*.

**`--rc`** *`PATH`*  
Path to the just-mrrc file to use. See **`just-mrrc`**(5) for more
details.  
Default: file path *`".just-mrrc"`* in the user's home directory.

**`--git`** *`PATH`*  
Path to the git binary in *`PATH`* or path to the git binary. Used in
the rare instances when shelling out to git is needed.  
Default: *`"git"`*.

**`--norc`**  
Option to prevent reading any **`just-mrrc`**(5) file.

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

This subcommand prepares all archive-type workspace roots for an offline
build by fetching all their required source files from the specified
locations given in the input configuration file. Any subsequent
**`just-mr`** or **`just`** invocations containing fetched archive workspace
roots will thus need no further network connections.

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

do
--

This subcommand is used as the canonical way of specifying just
arguments and calling **`just`** via **`execvp`**(2). Any subsequent argument
is unconditionally forwarded to **`just`**. For *known* subcommands
(**`version`**, **`describe`**, **`analyse`**, **`build`**, **`install`**, **`install-cas`**, **`rebuild`**), the
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

**`version`**|**`describe`**|**`analyse`**|**`build`**|**`install`**|**`install-cas`**|**`rebuild`**|**`gc`**
-------------------------------------------------------------------------------------------------------------

This subcommand is the explicit way of specifying *known* just
subcommands and calling **`just`** via **`execvp`**(2). The same description
as for the **`do`** subcommand applies.

EXIT STATUS
===========

The exit status of **`just-mr`** is one of the following values:

 - 0: the command completed successfully
 - 64: setup succeeded, but exec failed
 - 65: any unspecified error occurred in just-mr
 - 66: unknown subcommand
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

**`just-mr-repository-config`**(5),
**`just-repository-config`**(5),
**`just`**(1)
