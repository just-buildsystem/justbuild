Just-lock
=========

Status quo
----------

The canonical method of performing a multi-repository build using our build tool
is by running `just-mr` on a configuration file which has a well-defined format
and contains the description of all the repositories that should be considered
for the build. This file in many ways acts as a *lock file* of the project,
maintaining a snapshot in time of the repositories configuration. As with any
project lock file, this configuration file should be kept under version control
together with the source code, ensuring all project users can rely on the same
dependencies.

Dependencies for multi-repository *justbuild* projects require suitable
repository descriptions. For direct dependencies using *justbuild* one can, and
we argue should, rely on their committed configuration file. When such a
dependency is under Git version control, **`just-import-git`**(1) can take care
of the issue of repository composition, importing a given repository together
with its transitive dependencies from the target configuration. Multiple
dependencies are currently treated by multiple import tool calls, with the
common approach being to pipe the output of each call into the next, thanks to
a design that expects input at `stdin` and output to `stdout`. Typically,
deduplication of common transitive dependencies, which can otherwise lead to
configuration bloat, is handled by a final pipe into
**`just-deduplicate-repos`**(1).

While Git is a very popular version control system, it is by no means the only
option, alternatives such as CVS, Subversion, or Mercurial having also large
audiences. In such cases, a current available solution is to make the desired
content available locally and use `just add-to-cas` to import the content to
local CAS as a Git tree, for which then a corresponding repository description
can be written.

Some languages have their own methods of tracking and managing project
dependencies, which can and should be leveraged. For example, in the case of
*Rust*, **`just-import-cargo`**(1) uses *Cargo* to retrieve the dependencies of
a given Rust crate and generate the appropriate repository descriptions.

Shortcomings
------------

### Tooling limitations

The existing tools mentioned above are good at tackling their respective
use-cases, however they do come also with some limitations, the main one being
that they work only on individual repositories. This means that for projects
containing several *justbuild* dependencies (especially as our tool gains more
popularity) one needs to perform multiple imports. Complexity and loss of
readability increases quite fast with the current approach of chaining many
calls together and desirable features, like granular error reporting, require
more layers of scripting. Additionally, the status quo of addressing
side-effects of one tool by another (such as the already mentioned repository
deduplication case) can be improved by a unified framework.

### Fragmentation

The generation and maintenance of the configuration file of multi-repository
*justbuild* projects currently takes place in different manners, ranging from
direct manual editing to various user-made scripts wrapping calls to existing
tools. This has the main benefit that users have the highest liberty in deciding
which tools to use in handling each particular use-case when setting up or
updating the configuration file. On the other hand, choosing the best tool or
approach might not always be straight-forward, especially for newer users.

While currently the number of tools available to tackle various specific
scenarios is small, it is clear that with the continued development of
*justbuild* more use-cases will arise. Some of these are already known (such as
imports of non-Git sources or imports from local checkouts), some yet to be
discovered (for example, those that may come with the introduction of rules
support for more languages that have their own ecosystem). An approach to assist
future users can be, of course, to increase the current roster of tools. This
can however lead to a fragmented ecosystem around *justbuild* that handles to a
not inconsiderate extent the same task. On the other hand, being reticent in
introducing tooling options can be detrimental in attracting new users. As in
many cases, a balanced approach might offer the best of both worlds.

Other use-cases related to repository configuration are also not addressed by
current tooling. For example, two or more repositories might be logically
coupled, which, while not recommended, is a situation found in larger projects
for which a deep refactoring to decouple them can be prohibitively expensive.
In such a case, local development has to take place in more than one repository.
Creating local clones aware of the dependency closure of a repository
(completely defined in the configuration file) can be thus a useful feature.

Proposal: `Just-lock`
---------------------

We propose `just-lock` as a framework for **generating** and **maintaining**
the multi-repository configuration file of a *justbuild* project. This addresses
the mentioned shortcomings by providing a common interface for current import
functionalities, while allowing new tools to be freely used under very lax
conditions. This is achieved by means of a set of built-in import options,
describable in a well-defined format.

The framework revolves around the `just-lock` tool, which will implement all the
required functionality.

### The tool

Usage: generate/update a `just-mr` configuration file

```
just-lock [-C <repos.in.json>] [-o <repos.json>]
          [--local-build-root <PATH>]
          [--git <PATH>] [-L|--launcher <JSON>]
          [--clone <JSON>]

OPTIONS:
  -C PATH             Input file. If missing, searched for in ['repos.in.json', 'etc/repos.in.json']
                      with respect to the workspace root.
  -o PATH             Output file. If missing, searched for in ['repos.json', 'etc/repos.json'] with respect to
                      the workspace root. If none found, placed as 'repos.json' in parent path of input file.
  --local-build-root PATH
                      Local build root. Usual `just-mr` rules apply.
  --git PATH          Git binary to use if needed. If missing, system `git` is used.
                      User must pass it also to `just-mr`.
  --launcher JSON     Local launcher to use for commands. Given as a JSON list of strings.
                      If missing, ["env", "--"] is used. User must pass it also to `just-mr`.
  --clone JSON        Mapping from filesystem path to pair of repository name and a list of bindings.
                      For each map entry, the repository found by following the bindings from the given repository,
                      after all repositories have been imported, will be cloned in the specified filesystem directory and
                      its description in the output configuration made to point to this clone.
                      The specified repository names must be known, i.e., an initial repository or declared import,
                      and both the initial and target repositories will be kept during deduplication.
```

- Notes:

  The proposed default naming choice for the input file is chosen to mirror the
  default names of the configuration file of `just-mr`.

  The proposed search locations for the output configuration file, if an
  explicit path is not provided, are the same ones used by `just-mr` when run
  with the `--norc` option, but limited to the ones relative to the workspace
  root. This is done to better match the desired lock-file quality of the
  output file and also ensure `just-mr` can pick it up by default.

  The specification for finding the target repository for the `--clone` option
  uses the fact that the names of existing repositories and declared imports
  are the only ones known to remain as such in the output configuration, prior
  to deduplication, with all other repositories reachable from one such
  repository via a defined sequence of bindings. The clone locations are
  disjoint (as they are map keys), can be specified both absolute or relative
  to the current directory, and the referred to directory will be created if
  missing.

  The `--clone` option will produce an output configuration file meant for
  local development only. Therefore, it is not recommended for such a
  configuration file to be committed.

### Input file format

The input file describes which repositories will be part of the resulting
configuration file. The file is a `JSON` object. The proposed structure is:

``` jsonc
{ "main": "<name>"
, "repositories": {...}
, "imports": [...]
, "keep": [...]
}
```

The input file is expected to contain at most the mentioned 4 fields: `"main"`,
`"repositories"`, `"imports"`, and `"keep"`. Any other fields will be ignored.

The `"main"` and `"repositories"` fields maintain their meaning from the
usual `just-mr` configuration format (**`just-mr-repository-config`**(5)).
Therefore, neither fields are mandatory and missing fields are treated
consistently with `just-mr`. In this way, a `just-lock` input file containing
at most the `"main"` and `"repositories"` fields is a valid `just-mr`
configuration file. This subset of the input configuration object is referred
to in the following as the _core_ configuration.

For the input file of `just-lock` the `just-mr` format is simply extended with
**two** new fields. The value of the `"imports"` field is a list of `JSON`
objects describing a ***source***. Each _source_ provides information (in the
form of a well-defined set of fields) about how the _core_ configuration will
be extended. In most cases, this takes place by importing one or more
repositories from a specified existing `just-mr` configuration file, each with
their transitive dependencies, but more general options are available (as will
be described below). The imports are processed individually and consecutively
in the well-defined order declared in the input configuration file, meaning
that each import is extending the configuration obtained after processing all
preceding imports.

The format imposes a well-defined ordering of imports in order to maintain the
_naming convention_ already implicitly implemented by the existing tools. This
states that each import will add to the _core_ configuration only the following
repository names:
- the _name_ of the specified repository, which is well-defined by the input
  file format, and
- names starting with that _name_ followed by `"/"`, corresponding to the
  transitive dependencies of the specified repository.

This naming convention allows open names to be filled later in the import
sequence by repository names specified in the input file without the fear that
have been taken up during an earlier import.

If the `"main"` field is provided in the input file, it must match one of the
repository aliases marked for import or the name of one of the repositories
given by the `"repositories"` field.

The value of the `"keep"` field is a list of strings stating which repositories,
besides the one specified by `"main"` and those specified by option `--clone`,
are to be kept during the final _deduplication_ step, which takes place after
all imports have been processed. This way, `just-lock` will include all the
functionality `just-deduplicate-repos` provides. The output configuration file
of `just-lock` will always have deduplicated entries.

#### Proposed source types

The type of a _source_ is defined by the string value of the mandatory subfield
`"source"`.

- **git**

  This source type encompasses the functionality of `just-import-git`.

  We argue that most *justbuild* projects will contain one main configuration
  file, describing one or more repositories. This is why we propose a format
  that allows importing multiple repositories from the same source configuration
  file. Each declared repository is imported independently and consecutively,
  in the well-defined order provided by the user.
  
  If the `"commit"` field is missing, the `HEAD` commit of the specified remote
  branch will be considered. This will have an effect also on the fixed commit
  that will be used in the resulting repository description corresponding to any
  imported `"file"`-type repositories (see `just-import-git`).

  Proposed format:
  ``` jsonc
  { "source": "git"
  // "source"-specific fields
  // defines which repositories to import from source repository
  , "repos":                  // mandatory; list of repositories to be imported
    [ { "alias": "<name>"     // corresponds to `import_as` var (option --as);
                              // mandatory if "repo" value missing, otherwise value of "repo" taken if missing
      , "repo": "<foreign_name>"      // optional; corresponds to `foreign_repository_name` var
      , "map": {"from_name": "to_name"}     // optional; corresponds to `import_map` var (option --map)
      , "pragma":             // optional
        {"absent": true}      // corresponds to `absent` var (option --absent)
      }
    , ...
    ]
  // fields related to obtaining source config
  , "url": "https://nonexistent.example.com/repo.git"                 // mandatory
  , "mirrors": ["https://nonexistent-mirror.example.com/repo.git"]    // optional
  , "branch": "master"    // mandatory (as we have no sane default value between "master" and "main");
                          // corresponds to `branch` var (option -b)
  , "commit": "<HASH>"    // optional; if missing, take HEAD commit of branch
  , "inherit env": [...]                  // optional; corresponds to `inherit_env` var (option --inherit-env)
  , "config": "<foreign_repos.json>"      // optional; corresponds to `foreign_repository_config` var (option -R)
  , "as plain": false     // optional; corresponds to `plain` var (option --plain)
  }
  ```

- **file**

  This _source_ type behaves similarly to **git**, with the main difference
  being that the referenced source repository is not a Git remote, but a local
  checkout.

  The checkout is assumed to be maintained, so that `"file"`-type repositories
  marked to be imported can retain their type.

  Proposed format:
  ``` jsonc
  { "source": "file"
  // "source"-specific fields
  // defines which repositories to import from source repository
  , "repos":                  // mandatory; list of repositories to be imported
    [ { "alias": "<name>"     // corresponds to `import_as` var (option --as);
                              // mandatory if "repo" value missing, otherwise value of "repo" taken if missing
      , "repo": "<foreign_name>"        // optional; corresponds to `foreign_repository_name` var
      , "map": {"from_name": "to_name"}     // optional; corresponds to `import_map` var (option --map)
      , "pragma":             // optional
        {"absent": true}      // corresponds to `absent` var (option --absent)
      }
    , ...
    ]
  // fields related to obtaining source config
  , "path": "<source/repo/path>"          // mandatory
  , "config": "<foreign_repos.json>"      // optional; corresponds to `foreign_repository_config` var (option -R)
  , "as plain": false         // optional; corresponds to `plain` var (option --plain)
  }
  ```

- **archive**

  This _source_ type behaves similarly to **git**, with the main difference
  being that the referenced source repository is not a Git remote, but an
  archive, such as a release tarball.

  A field `"subdir"` is provided to account for the fact that source repository
  root often is not the root directory of the unpacked archive.

  Proposed format:
  ``` jsonc
  { "source": "archive"
  // "source"-specific fields
  // defines which repositories to import from source repository
  , "repos":                  // mandatory; list of repositories to be imported
    [ { "alias": "<name>"     // corresponds to `import_as` var (option --as);
                              // mandatory if "repo" value missing, otherwise value of "repo" taken if missing
      , "repo": "<foreign_name>"        // optional; corresponds to `foreign_repository_name` var
      , "map": {"<from_name>": "<to_name>"}      // optional; corresponds to `import_map` var (option --map)
      , "pragma":             // optional
        {"absent": true}      // corresponds to `absent` var (option --absent)
      }
    , ...
    ]
  // fields related to obtaining source config
  , "fetch": "<URL>"          // mandatory
  , "type": "tar|zip"         // optional; type of archive in set ["tar", "zip"]; if missing, default to "tar"
  , "mirrors": ["..."]        // optional
  , "subdir": "<REL_PATH>"    // optional; relative path defining the actual root of the source repository;
                              // if missing, the source root is the root directory of the unpacked archive
  , "content": "<HASH>"       // optional; if missing, always fetch; if given, will be checked
  , "sha256": "<HASH>"        // optional checksum; if given, will be checked
  , "sha512": "<HASH>"        // optional checksum; if given, will be checked
  , "config": "<foreign_repos.json>"      // optional; corresponds to `foreign_repository_config` var (option -R)
  , "as plain": false         // optional; corresponds to `plain` var (option --plain)
  }
  ```

- **git tree**

  This _source_ type proposed to be the canonical way of importing *justbuild*
  dependencies under version control systems other than Git.

  The command that produces the tree is either given explicitly (field `"cmd"`)
  or indirectly by a command-generating command (field `"cmd gen"`). The tool
  will run the so-given command to produce the content in a temporary directory,
  it will import the given subdirectory to Git, and it will generate a
  corresponding `"git tree"`-type repository description to be added to the
  configuration.

  The fields `"cmd"`, `"env"`, `"inherit env"` have the same meaning as those
  of the `"git tree"`-type repository (as per `just-mr-repository-config`).

  **IMPORTANT:** The user has to be the one to ensure that the environment in
  which `just-lock` is run matches the one intended for running `just-mr` with
  respect to all the provided envariables in the `"inherit env"` list. This is
  because `just-lock` and `just-mr` must produce the same tree when running the
  same command.

  NOTE: While the target configuration file has to be part of the specified
  `"subdir"` tree, referenced `"file"`-type repositories marked to be imported
  can point also outside of the `"subdir"`, as long as they are still contained
  in the initial checkout (i.e., the directory generated by the command). All
  such repositories will be translated to appropriate `"git tree"`-type
  repositories in the output configuration.

  Proposed format:
  ``` jsonc
  { "source": "git tree"
  // "source"-specific fields
  , "repos":                      // mandatory; list of entries describing repositories to import
    [ { "alias": "<name>"         // mandatory; same meaning as `import_as` var
                                  // mandatory if "repo" value missing, otherwise value of "repo" taken if missing
      , "repo": "<foreign_name>"  // optional; same meaning as `foreign_repo_name` var
      , "map": {"<from_name>": "<to_name>"}      // optional; corresponds to `import_map` var (option --map)
      , "pragma":                 // optional
        {"absent": true}          // same meaning as `absent` var
      }
    ]
  , "cmd": [...]              // one and only one of {"cmd", "cmd_gen"} must be provided;
                              // command as list of strings
  , "subdir": "<subdir>"      // optional; default is "."; subdir to consider as main entry point
  , "env": {...}              // optional; map of envariables needed by "cmd"
  , "inherit env": [...]      // optional; list of envariables to inherit
  , "cmd gen": [...]          // one and only one of {"cmd", "cmd_gen"} must be provided;
                              // command producing the "cmd" value to use, as list of strings
  , "config": "<foreign_repos.json>"      // optional; corresponds to `foreign_repository_config` var (option -R)
                                          // searched for in the "subdir" tree
  , "as plain": false         // optional; corresponds to `plain` var (option --plain)
  }
  ```

- **generic**

  This _source_ type is proposed to be the canonical way for users to provide
  their own command which can update a `just-mr` configuration.
  
  The command must accept a `just-mr` configuration as input from `stdin` and
  must output a `just-mr` configuration to `stdout`. The command is run in a
  given subpath of the current directory (by default `"."`) and as such can have
  side-effects on the filesystem.
  
  The input fed to the command is the _current_ configuration, i.e., the
  configuration obtained after processing all preceding imports (according to
  the well-defined order declared in the input configuration file). The output
  configuration is used as input for the succeeding import.

  The user must take care to correctly construct the `"imports"` list in order
  to process **generic** entries at the desired time. For example, if a
  **generic** entry needs to be process between the import of two repositories
  from the same **git** source, the user must split that **git** source into two
  corresponding **git**  entries and place the **generic** entry between them.

  The calling environment is inherited.

  Proposed format:
  ``` jsonc
  { "source": "generic"
  // "source"-specific fields
  , "cwd": "<path>"       // optional; relative path to run the script in;
                          // if missing, defaults to "."
  , "cmd": [...]          // mandatory; command to run, as list of strings
  , "env": {...}          // optional; map of envariables needed by script
  }
  ```
