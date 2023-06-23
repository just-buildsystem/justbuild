% JUST-IMPORT-GIT(1) | General Commands Manual

NAME
====

just-import-git - import one git repository to a multi-repository
configuration

SYNOPSIS
========

**`just-import-git`** \[*`OPTION`*\]... *`URL`* \[*`foreign repository name`*\]  

DESCRIPTION
===========

Extend an existing **`just-mr-repository-config`**(5) by adding one git
repository. In doing so, the dependencies declared in the imported
repository are added as well and *`"file"`* repositories are transformed
to *`"subdir"`* parts of the imported repository. This solves the
problem, that a repository can refer to itself only as *`"."`* in a
portable way. The importing party, however, always knows the URL it is
importing from.

The imported repositories are renamed in a way that no conflicts with
already present repositories arise. The repositories pulled in as
dependencies are named in a way to remind for which repositories they
came as a dependency. This renaming is taken into account at all places
repositories are referred to, i.e., the *`"bindings"`* field, as well as
roots defined by reference to other repositories.

Only the main parts of repositories are imported (*`"repository"`*,
*`"bindings"`*, names, and roots). The *`"pragma"`* part, as well as
unknown fields are dropped.

The repository to import is specified by its URL

The resulting configuration is printed on standard output.

OPTIONS
=======

**`--as`** *`NAME`*  
Specify the name the imported repository should have in the final
configuration. If not specified, default to the name the repository has
in the configuration file of the repository imported. In any case, the
name is amended if it conflicts with an already existing name.

**`-b`** *`BRANCH`*  
The branch in the imported repository to use; this branch is also
recorded as the branch to follow. Defaults to *`"master"`*.

**`-C`** *`CONFIGFILE`*  
Use the specified file as the configuration to import into. The string
*`-`* is treated as a request to take the config from stdin; so a file
called *`-`* has to be described as *`.`*/*`-`*, or similar. If not
specified, for a config file is searched in the same way, as **`just-mr`**
does, when invoved with **`--norc`**.

**`-h`**, **`--help`**  
Output a usage message and exit.

**`--map`** *`THEIRS`* *`OURS`*  
Map repositories from the imported configuration to already existing
ones. Those repositories are not imported (and the search for their
transitive dependency is omitted) and instead the specified already
existing repository is used. This is useful, if a sufficiently
compatible repository already exists in the configuration.

**`-R`** *`RELPATH`*  
Use the file, specified by path relative to the repository root, as
multi-repository specification in the imported repository. If not
specifed, for a config file is searched in the same way as **`just-mr`**
does, when invoked with **`--no-rc`**, however leaving out searches
relative to global roots (*`"home"`* and *`"system"`*). In other words,
*`repos.json`* and *`etc/repos.json`* are tried if this option is not
given.

**`--plain`**  
Pretend the foreign multi-repository specification is the canonical one
for a single repository. Useful, if the repository to be imported does
not have a repository configuration or should be imported without
dependencies.

See also
========

**`just-mr-repository-config`**(5),
**`just-mr`**(1)
