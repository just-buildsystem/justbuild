% JUST-DEDUPLICATE-REPOS(1) | General Commands Manual

NAME
====

just-deduplicate-repos - remove duplicate repositories from a
multi-repository configuration

SYNOPSIS
========

**`just-deduplicate-repos`**  \[*`repository name`*\]...  

DESCRIPTION
===========

Read a multi-repository configuration from stdin and print to stdout
a repository configuration with indistinguishable repositories merged
to a single repository. In doing so, keep the `"main"` repository,
as well as any repositories specified as arguments on
the command line, even if that means that some indistinguishable
repositories cannot be merged into one (as both have to be kept).

RATIONALE
=========

As **`just`**(1) is a multi-repository build system, typically
imported dependencies also contain multi-repository set up. Hence,
a project typically has three components to describe the involved
logical repositories.

First, there is a description of the local repositories, i.e.,
the logical repositories (as `"file"` repositories) that reside
in this physical repository in the sense of the version-control
system (typically **`git`**(1)). They are described in a file often
called `etc/repos.template.json` in **`just-mr-repository-config`**(5)
format with open names for the direct dependencies.

Next, there is a description of where to get the direct dependencies
and which branches to follow there. This description is typically
a script piping said `repos.template.json` through a sequence of
invocations of **`just-import-git`**(1) which also adds the indirect
dependencies.

Finally, the output of that script is the multi-repository
configuration `etc/repos.json` for this project. This file, while
being generated, is still committed into the repository. First
of all, it contains additional information: the precise pinned
versions of (all) the dependencies which might change over time.
So committing this file allows others to build with the precise
same dependencies (including for old versions of the project).
Moreover, having the multi-repository configuration materialized
in the repository allows offline builds once the dependencies have
been fetched (possibly by a different project) to **`just-mr`**(1)'s
local build root (possibly by the setup for a different project
that happens to have those dependencies as well). To update the
dependencies, the import script can be run again and the newly
created `etc/repos.json` committed in an update commit (after
verifying that this update does not break anything). The final
reason to commit a generated `etc/repos.json` is to close the loop
and allow this project to be the dependency of another one; to keep
the work done by **`just-import-git`**(1) manageable it requires the
imported repository to have all its (direct or indirect) dependencies
described precisely in their multi-repository config.

When a project has more than one direct dependency, it can happen
that two of the direct dependencies have a common dependency. Simply
chaining **`just-import-git`**(1) this dependency will end up twice
in the final repository configuration. While this will not result
in additional actions, it will increase the cost of the analysis.
Moreover, not merging indistinguishable repositories will make the
resulting `etc/repos.json` bigger and propagate those redundant
copies to other projects importing this one; that mechanism can,
over a long chain of imports, lead to exponential many of those
redundant copies. To avoid this, **`just-deduplicate-repos`** can
be added as a last step (before the JSON pretty printing) in the
import script.

See also
========

**`just-mr-repository-config`**(5),
**`just-import-git`**(1)
**`just-mr`**(1)
