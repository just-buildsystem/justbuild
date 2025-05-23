Multi-repository configuration management: `just-lock`
======================================================

The multi-repository build configuration used as input by `just-mr` acts for
all intents and purposes as a _lockfile_ for *justbuild* projects, containing
the pinned versions of all content-fixed repositories. This is expected to be
stored and shipped together with the source code, allowing a consistent
development environment for all users.

With dependencies of projects coming in many shapes and forms, `just-lock` is
a tool for generating and maintaining the multi-repositories configuration 
(lock)file of *justbuild* projects. This tool performs several functionalities,
such as importing dependencies with repository composition (extending the
functionality available in the `just-import-git` tool), automatic deduplication
of identical transitive dependencies (a functionality available also standalone
in the `just-deduplicate-repos` tool), or setup of repository clones for local
development. For the purposes of this tutorial, we will focus only on the
dependency import aspects.

Basic use of `just-lock`
------------------------

In order to produce the multi-repository build configuration file, `just-lock`
expects an input configuration file. The format of this file is an extension of
the one of `just-mr` and it is read as a `JSON` object. The file defines four
elements:

 - the description of local repositories, as the value for mandatory field
 `"repositories"`. This usually defines local checkouts, patches, overlays.
 - the description of remote dependencies, as the optional value for field
 `"imports"`. For *justbuild* dependencies, their multi-repository configuration
 (lock)file is taken as the ground truth for importing repository descriptions
 into the current project. Non-*justbuild* dependencies can also be described
 here, but can only be imported as-is, with overlays defining how such source
 code should be integrated (as it will be shown below).
 - the repository to consider as the main entry point for the build, as the
 value of the optional field `"main"`.
 - a list of repository names, as the value of the field `"keep"`. These names
 will be kept in the final configuration during the automatic process of
 deduplicating any repeated repositories brought in as transitive dependencies
 from different imports. For the purposes of this tutorial, the use of this
 field is not exemplified.

### The input file

For the following, we return to the *hello_world* example from the section on
[*Building Third-party dependencies*](./third-party-software.md), which depends
on the open-source project [fmtlib](https://github.com/fmtlib/fmt), in the
setup that enables high-level target caching.

We define the following `repos.in.json` input configuration file for
`just-lock`:

``` {.jsonc srcname="repos.in.json"}
{ "main": "tutorial"
, "repositories":
  { "rules-cc":
    { "repository": "rules-cc-rules-sources"
    , "target_root": "tutorial-defaults"
    , "rule_root": "rules-cc"
    }
  , "tutorial":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"rules": "rules-cc", "format": "fmtlib"}
    }
  , "tutorial-defaults":
    { "repository":
      { "type": "file"
      , "path": "./tutorial-defaults"
      , "pragma": {"to_git": true}
      }
    }
  , "fmt-targets-layer":
    { "repository":
      { "type": "file"
      , "path": "./fmt-layer"
      , "pragma": {"to_git": true}
      }
    }
  , "fmtlib":
    { "repository": "fmtlib-sources"
    , "target_root": "fmt-targets-layer"
    , "bindings": {"rules": "rules-cc"}
    }
  }
, "imports":
  [ { "source": "git"
    , "branch": "master"
    , "commit": "7a2fb9f639a61cf7b7d7e45c7c4cea845e7528c6"
    , "url": "https://github.com/just-buildsystem/rules-cc.git"
    , "repos": [{"alias": "rules-cc-rules-sources", "repo": "rules"}]
    }
  , { "source": "git"
    , "branch": "8.1.1"
    , "url": "https://github.com/fmtlib/fmt.git"
    , "repos": [{"alias": "fmtlib-sources"}]
    , "as plain": true
    }
  ]
}
```

As already mentioned, the `"imports"` field provides a description of the
external (usually third-party) dependencies of a project. In our case, we
use the `rules-cc` and `fmtlib` libraries, described as `git` sources.

The first thing to note is the use of overlays. In our simple project, as we
do not want to import anything other than the source trees of the needed
dependencies, we create the `"rules-cc-rules-sources"` and `"fmtlib-sources"`
names, which will bind the workspace roots of our `"rules-cc"` and `"fmtlib"`
repositories, respectively, to the descriptions of the remote repositories
providing the necessary source trees.

The first import description object is for `rules-cc`, which is a *justbuild*
project hosted as a Git repository, from which we would like to import its
`"rules"` subdirectory. Thankfully, that project offers a useful shorthand by
defining in its own locked repositories configuration file the `"rules"` overlay
repository pointing to the respective subdirectory. `just-lock` will read that
configuration file in order to produce the resulting configuration, caching any
fetched source trees. It is thus highly recommended that the same build root as
the one subsequently used by `just-mr` is provided to `just-lock` (via the
`--local-build-root` option, with same default behaviour as in `just-mr`).

In the case of `fmtib`, which is also hosted as a Git repository but does not
provide a *justbuild* configuration file, we can only import it as-is, as a
complete repository, signaled by setting the `"as plain"` flag. Do note that in
this case we can limit ourselves to also just providing the `"branch"` field,
and not also a specific commit. This is because `just-lock` automatically
interrogates the remote in order to retrieve the top commit associated to a
certain reference (in this case, a release tag) and pin it to a hard reference
(in this case, the commit identifier) into the output configuration. This is a
useful feature, as it is often the case that information about dependencies
comes in the form of "loose" references, such as release tags or even simply the
remote location of a distfile, but which `just-lock` then will pin down to a
content-defined reference, such as a commit, blob, or tree identifier.

### Generating the configuration

We generate an output configuration file `repos.out.json` by running `just-lock`
with the appropriate arguments, then we build the `helloworld` target with this
configuration:

``` sh
$ just-lock -C repos.in.json -o repos.out.json
[...]
$
$ just-mr -C repos.out.json build helloworld
INFO: Performing repositories setup
INFO: Found 5 repositories involved
INFO: Setup finished, exec ["just","build","-C","...","helloworld"]
INFO: Requested target is [["@","tutorial","","helloworld"],{}]
INFO: Analysed target [["@","tutorial","","helloworld"],{}]
INFO: Export targets found: 1 cached, 0 uncached, 0 not eligible for caching
INFO: Discovered 4 actions, 0 tree overlays, 2 trees, 0 blobs
INFO: Building [["@","tutorial","","helloworld"],{}].
INFO: Processed 4 actions, 4 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [18d25e828a0176cef6fb029bfd83e1862712ec87:132736:x]
$
```

As it can be seen, everything comes from cache and the new configuration behaves
identical to the manually written one. If available, one can inspect the
difference in content between the two files using the `jq` command-line tool:

``` sh
$ diff -y <(jq --sort-keys . repos.out.json) <(jq --sort-keys . repos.json)
{                                                               {
  "main": "tutorial",                                             "main": "tutorial",
  "repositories": {                                               "repositories": {
    "fmt-targets-layer": {                                          "fmt-targets-layer": {
      "repository": {                                                 "repository": {
        "path": "./fmt-layer",                                          "path": "./fmt-layer",
        "pragma": {                                                     "pragma": {
          "to_git": true                                                  "to_git": true
        },                                                              },
        "type": "file"                                                  "type": "file"
      }                                                               }
    },                                                              },
    "fmtlib": {                                                     "fmtlib": {
      "bindings": {                                                   "bindings": {
        "rules": "rules-cc"                                             "rules": "rules-cc"
      },                                                              },
      "repository": "fmtlib-sources",                         <
      "target_root": "fmt-targets-layer"                      <
    },                                                        <
    "fmtlib-sources": {                                       <
      "repository": {                                                 "repository": {
        "branch": "8.1.1",                                              "branch": "8.1.1",
        "commit": "b6f4ceaed0a0a24ccf575fab6c56dd50ccf6f1a9",           "commit": "b6f4ceaed0a0a24ccf575fab6c56dd50ccf6f1a9",
        "repository": "https://github.com/fmtlib/fmt.git",              "repository": "https://github.com/fmtlib/fmt.git",
        "type": "git"                                                   "type": "git"
      }                                                       |       },
                                                              >       "target_root": "fmt-targets-layer"
    },                                                              },
    "rules-cc": {                                                   "rules-cc": {
      "repository": "rules-cc-rules-sources",                 <
      "rule_root": "rules-cc",                                <
      "target_root": "tutorial-defaults"                      <
    },                                                        <
    "rules-cc-rules-sources": {                               <
      "repository": {                                                 "repository": {
        "branch": "master",                                             "branch": "master",
        "commit": "7a2fb9f639a61cf7b7d7e45c7c4cea845e7528c6",           "commit": "7a2fb9f639a61cf7b7d7e45c7c4cea845e7528c6",
        "repository": "https://github.com/just-buildsystem/ru           "repository": "https://github.com/just-buildsystem/ru
        "subdir": "rules",                                              "subdir": "rules",
        "type": "git"                                                   "type": "git"
      }                                                       |       },
                                                              >       "rule_root": "rules-cc",
                                                              >       "target_root": "tutorial-defaults"
    },                                                              },
    "tutorial": {                                                   "tutorial": {
      "bindings": {                                                   "bindings": {
        "format": "fmtlib",                                             "format": "fmtlib",
        "rules": "rules-cc"                                             "rules": "rules-cc"
      },                                                              },
      "repository": {                                                 "repository": {
        "path": ".",                                                    "path": ".",
        "type": "file"                                                  "type": "file"
      }                                                               }
    },                                                              },
    "tutorial-defaults": {                                          "tutorial-defaults": {
      "repository": {                                                 "repository": {
        "path": "./tutorial-defaults",                                  "path": "./tutorial-defaults",
        "pragma": {                                                     "pragma": {
          "to_git": true                                                  "to_git": true
        },                                                              },
        "type": "file"                                                  "type": "file"
      }                                                               }
    }                                                               }
  }                                                               }
}                                                               }
```

Except for the two overlays, kept from the `just-lock` input file configuration,
the two configuration files have the same content.
