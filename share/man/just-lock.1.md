% JUST-LOCK(1) | General Commands Manual

NAME
====

just-lock - generate and maintain a multi-repository configuration file

SYNOPSIS
========

**`just-lock`** \[*`OPTION`*\]...

DESCRIPTION
===========

Just-Lock is a tool that encompasses several functionalities related to
generating and maintaining the **`just-mr-repository-config`**(5) of a Just
project.

The main functionality of the tool is to import declared dependencies from
other Just projects and generate a repository configuration which can directly
be used by **`just-mr`**(1). The imported repositories are renamed in a way that
no conflicts arise and in a way to remind for which repositories they come as a
dependency. This extends thus the capabilities of existing tools such as
**`just-import-git`**(1) by implicitly allowing multiple imports to take place
and import sources other than Git repositories.

By default, the final configuration has the repositories deduplicated, by
merging indistinguishable repositories, other than the `"main"` repository and
explicitly stated ones, to a single repository. This mirrors the capability
available standalone in **`just-deduplicate-repos`**(1).

The tool performs these operations based on a provided **`just-lock-config`**(5)
input file and outputs the resulting configuration file at either a given
provided location or at a location expected by **`just-mr`**(5).

OPTIONS
=======

**`-h`**, **`--help`**  
Output a usage message and exit.

**`-C`** *`CONFIGFILE`*  
Use the specified file as the input **`just-lock-config`**(5) file.
If not specified, a file with filename `repos.in.json` is searched for in the
same _directories_ as **`just-mr`** does when invoked with **`--norc`** when
searching for its configuration file.

**`-o`** *`CONFIGFILE`*  
Use the specified file as the output **`just-mr-repository-config`**(5) file.
If not specified, a file is searched for in the same way **`just-mr`** does
when invoked with **`--norc`**. If none found, it is a file with filename
`repos.json` in the parent directory of the input configuration file.

**`--local-build-root`** *`PATH`*  
Root for local CAS, cache, and build directories. The path will be created if
it does not exist already.  
Default: path *`".cache/just"`* in user's home directory.

**`-L`**, **`--local-launcher`** *`JSON_ARRAY`*  
JSON array with the list of strings representing the launcher to prepend
any commands being executed locally.  
Default: *`["env", "--"]`*.

**`--git`** *`PATH`*  
Path to the git binary in *`PATH`* or path to the git binary. Used when
shelling out to git is needed.  
Default: *`"git"`*.

See also
========

**`git`**(1),
**`just-lock-config`**(5),
**`just-import-git`**(1),
**`just-deduplicate-repos`**(1),
**`just-mr-repository-config`**(5),
**`just-mr`**(1)
