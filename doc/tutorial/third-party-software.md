Building Third-party Software
=============================

Third-party projects usually ship with their own build description,
which often happens to not be compatible with *justbuild*. Nevertheless,
it is often desireable to include external projects via their source
code base, instead of relying on the integration of out-of-band binary
distributions. *justbuild* offers a flexible approach to provide the
required build description via an overlay layer without the need to
touch the original code base. This mechanism is independent of the
actual *justbuild* description eventually used, and the latter might
well be a
[rule calling a foreign buildsystem](https://github.com/just-buildsystem/rules-cc#rule-ccforeigncmake-library).
In this section, however, we describe the cleaner approach of providing
a native build description.

For the remainder of this section, we expect to have the project files
available resulting from successfully completing the tutorial section on
[*Building C++ Hello World*](./hello-world.md). We will demonstrate how to use
the open-source project [fmtlib](https://github.com/fmtlib/fmt) as an
example for integrating third-party software to a *justbuild* project.

Creating the target overlay layer for fmtlib
--------------------------------------------

Before we construct the overlay layer for fmtlib, we need to determine
its file structure ([tag
8.1.1](https://github.com/fmtlib/fmt/tree/8.1.1)). The relevant header
and source files are structured as follows:

    fmt
    |
    +--include
    |  +--fmt
    |     +--*.h
    |
    +--src
       +--format.cc
       +--os.cc

The public headers can be found in `include/fmt`, while the library's
source files are located in `src`. For the overlay layer, the `TARGETS`
files should be placed in a tree structure that resembles the original
code base's structure. It is also good practice to provide a top-level
`TARGETS` file, leading to the following structure for the overlay:

    fmt-layer
    |
    +--TARGETS
    +--include
    |  +--fmt
    |     +--TARGETS
    |
    +--src
       +--TARGETS

Let's create the overlay structure:

``` sh
$ mkdir -p ./fmt-layer/include/fmt
$ mkdir -p ./fmt-layer/src
```

The directory `include/fmt` contains only header files. As we want all
files in this directory to be included in the `"hdrs"` target, we can
safely use the explicit `TREE` reference[^1], which collects, in a
single artifact (describing a directory) *all* directory contents from
`"."` of the workspace root. Note that the `TARGETS` file is only part
of the overlay, and therefore will not be part of this tree.
Furthermore, this tree should be staged to `"fmt"`, so that any consumer
can include those headers via `<fmt/...>`. The resulting header
directory target `"hdrs"` in `include/fmt/TARGETS` should be described
as:

``` {.jsonc srcname="fmt-layer/include/fmt/TARGETS"}
{ "hdrs":
  { "type": ["@", "rules", "data", "staged"]
  , "srcs": [["TREE", null, "."]]
  , "stage": ["fmt"]
  }
}
```

The actual library target is defined in the directory `src`. For the
public headers, it refers to the previously created `"hdrs"` target via
its fully-qualified target name (`["include/fmt", "hdrs"]`). Source
files are the two local files `format.cc`, and `os.cc`. The final target
description in `src/TARGETS` will look like this:

``` {.jsonc srcname="fmt-layer/src/TARGETS"}
{ "fmt":
  { "type": ["@", "rules", "CC", "library"]
  , "name": ["fmt"]
  , "hdrs": [["include/fmt", "hdrs"]]
  , "srcs": ["format.cc", "os.cc"]
  }
}
```

Finally, the top-level `TARGETS` file can be created. While it is
technically not strictly required, it is considered good practice to
*export* every target that may be used by another project. Exported
targets are subject to high-level target caching, which allows to skip
the analysis and traversal of entire subgraphs in the action graph.
Therefore, we create an export target that exports the target
`["src", "fmt"]`, with only the variables in the field
`"flexible_config"` being propagated.
The top-level `TARGETS` file contains the following content:

``` {.jsonc srcname="fmt-layer/TARGETS"}
{ "fmt":
  { "type": "export"
  , "target": ["src", "fmt"]
  , "flexible_config": ["CXX", "CXXFLAGS", "ADD_CXXFLAGS", "AR", "ENV", "DEBUG"]
  }
}
```

After adding the library to the multi-repository configuration (next
step), the list of configuration variables a target, like `["src",
"fmt"]`, actually depends on can be obtained using the `--dump-vars`
option of the `analyse` subcommand. In this way, an informed decision
can be taken when deciding which variables of the export target to make
tunable for the consumer.

Adding fmtlib to the Multi-Repository Configuration
---------------------------------------------------

Based on the *hello world* tutorial, we can extend the existing
`repos.json` by the layer definition `"fmt-targets-layer"` and the
repository `"fmtlib"`, which is based on the Git repository with its
target root being overlayed. Furthermore, we want to use `"fmtlib"` in
the repository `"tutorial"`, and therefore need to introduce an
additional binding `"format"` for it:

``` {.jsonc srcname="repos.json"}
{ "main": "tutorial"
, "repositories":
  { "rules-cc":
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "3a5f0f0f50c59495ffc3b198df59e6edb8416450"
      , "repository": "https://github.com/just-buildsystem/rules-cc.git"
      , "subdir": "rules"
      }
    , "target_root": "tutorial-defaults"
    , "rule_root": "rules-cc"
    }
  , "tutorial":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"rules": "rules-cc", "format": "fmtlib"}
    }
  , "tutorial-defaults":
    { "repository": {"type": "file", "path": "./tutorial-defaults"}
    }
  , "fmt-targets-layer":
    { "repository": {"type": "file", "path": "./fmt-layer"}
    }
  , "fmtlib":
    { "repository":
      { "type": "git"
      , "branch": "8.1.1"
      , "commit": "b6f4ceaed0a0a24ccf575fab6c56dd50ccf6f1a9"
      , "repository": "https://github.com/fmtlib/fmt.git"
      }
    , "target_root": "fmt-targets-layer"
    , "bindings": {"rules": "rules-cc"}
    }
  }
}
```

This `"format"` binding can be used to add a new private dependency
in `greet/TARGETS`:

``` {.jsonc srcname="greet/TARGETS"}
{ "greet":
  { "type": ["@", "rules", "CC", "library"]
  , "name": ["greet"]
  , "hdrs": ["greet.hpp"]
  , "srcs": ["greet.cpp"]
  , "stage": ["greet"]
  , "private-deps": [["@", "format", "", "fmt"]]
  }
}
```

Consequently, the `fmtlib` library can now be used by `greet/greet.cpp`:

``` {.cpp srcname="greet/greet.cpp"}
#include "greet.hpp"
#include <fmt/format.h>

void greet(std::string const& s) {
  fmt::print("Hello {}!\n", s);
}
```

Due to changes made to `repos.json`, building this tutorial requires to
rerun `just-mr`, which will fetch the necessary sources for the external
repositories:

``` sh
$ just-mr build helloworld
INFO: Performing repositories setup
INFO: Found 5 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","helloworld"]
INFO: Requested target is [["@","tutorial","","helloworld"],{}]
INFO: Analysed target [["@","tutorial","","helloworld"],{}]
INFO: Export targets found: 0 cached, 0 uncached, 1 not eligible for caching
INFO: Discovered 7 actions, 3 trees, 0 blobs
INFO: Building [["@","tutorial","","helloworld"],{}].
INFO: Processed 7 actions, 1 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [18d25e828a0176cef6fb029bfd83e1862712ec87:132736:x]
$
```

Note that in order to build the `fmt` target alone, its containing
repository `fmtlib` must be specified via the `--main` option:

``` sh
$ just-mr --main fmtlib build fmt
INFO: Performing repositories setup
INFO: Found 4 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","fmt"]
INFO: Requested target is [["@","fmtlib","","fmt"],{}]
INFO: Analysed target [["@","fmtlib","","fmt"],{}]
INFO: Export targets found: 0 cached, 0 uncached, 1 not eligible for caching
INFO: Discovered 3 actions, 1 trees, 0 blobs
INFO: Building [["@","fmtlib","","fmt"],{}].
INFO: Processed 3 actions, 3 cache hits.
INFO: Artifacts built, logical paths are:
        libfmt.a [513b2ac17c557675fc841f3ebf279003ff5a73ae:240914:f]
      (1 runfiles omitted.)
$
```

Employing high-level target caching
-----------------------------------

To make use of high-level target caching for exported targets, we need
to ensure that all inputs to an export target are transitively
content-fixed. This is automatically the case for `"type":"git"`
repositories. However, the `libfmt` repository also depends on
`"rules-cc"`, `"tutorial-defaults"`, and `"fmt-target-layer"`. As the
latter two are `"type":"file"` repositories, they must be put under Git
versioning first:

``` sh
$ git init .
$ git add tutorial-defaults fmt-layer
$ git commit -m "fix compile flags and fmt targets layer"
[master (root-commit) 9c3a98b] fix compile flags and fmt targets layer
 4 files changed, 29 insertions(+)
 create mode 100644 fmt-layer/TARGETS
 create mode 100644 fmt-layer/include/fmt/TARGETS
 create mode 100644 fmt-layer/src/TARGETS
 create mode 100644 tutorial-defaults/CC/TARGETS
```

Note that `rules-cc` already is under Git versioning.

Now, to instruct `just-mr` to use the content-fixed, committed source
trees of those `"type":"file"` repositories the pragma `"to_git"` must
be set for them in `repos.json`:

``` {.jsonc srcname="repos.json"}
{ "main": "tutorial"
, "repositories":
  { "rules-cc":
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "3a5f0f0f50c59495ffc3b198df59e6edb8416450"
      , "repository": "https://github.com/just-buildsystem/rules-cc.git"
      , "subdir": "rules"
      }
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
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "b6f4ceaed0a0a24ccf575fab6c56dd50ccf6f1a9"
      , "repository": "https://github.com/fmtlib/fmt.git"
      }
    , "target_root": "fmt-targets-layer"
    , "bindings": {"rules": "rules-cc"}
    }
  }
}
```

Due to changes in the repository configuration, we need to rebuild and
the benefits of the target cache should be visible on the second build:

``` sh
$ just-mr build helloworld
INFO: Performing repositories setup
INFO: Found 5 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","helloworld"]
INFO: Requested target is [["@","tutorial","","helloworld"],{}]
INFO: Analysed target [["@","tutorial","","helloworld"],{}]
INFO: Export targets found: 0 cached, 1 uncached, 0 not eligible for caching
INFO: Discovered 7 actions, 3 trees, 0 blobs
INFO: Building [["@","tutorial","","helloworld"],{}].
INFO: Processed 7 actions, 7 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [18d25e828a0176cef6fb029bfd83e1862712ec87:132736:x]
INFO: Backing up artifacts of 1 export targets
$
$ just-mr build helloworld
INFO: Performing repositories setup
INFO: Found 5 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","helloworld"]
INFO: Requested target is [["@","tutorial","","helloworld"],{}]
INFO: Analysed target [["@","tutorial","","helloworld"],{}]
INFO: Export targets found: 1 cached, 0 uncached, 0 not eligible for caching
INFO: Discovered 4 actions, 2 trees, 0 blobs
INFO: Building [["@","tutorial","","helloworld"],{}].
INFO: Processed 4 actions, 4 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [18d25e828a0176cef6fb029bfd83e1862712ec87:132736:x]
$
```

Note that in the second run the export target `"fmt"` was taken from
cache and its 3 actions were eliminated, as their result has been
recorded to the high-level target cache during the first run.

Also note the final message in the first run. As that was the first time the
export target `"fmt"` was built (i.e., target `"fmt"` with default
configuration flags), an entry in the target-level cache was created. The
log message showcases that when a remote-execution endpoint is involved, any
artifacts referenced by a built export target needs to be ensured to be
available.

Combining overlay layers for multiple projects
----------------------------------------------

Projects typically depend on multiple external repositories. Creating an
overlay layer for each external repository might unnecessarily clutter
up the repository configuration and the file structure of your
repository. One solution to mitigate this issue is to combine the
`TARGETS` files of multiple external repositories in a single overlay
layer. To avoid conflicts, the `TARGETS` files can be assigned different
file names per repository. As an example, imagine a common overlay layer
with the files `TARGETS.fmt` and `TARGETS.gsl` for the repositories
`"fmtlib"` and `"gsl-lite"`, respectively:

    common-layer
    |
    +--TARGETS.fmt
    +--TARGETS.gsl
    +--include
    |  +--fmt
    |  |  +--TARGETS.fmt
    |  +--gsl
    |     +--TARGETS.gsl
    |
    +--src
       +--TARGETS.fmt

Such a common overlay layer can be used as the target root for both
repositories with only one difference: the `"target_file_name"` field.
By specifying this field, the dispatch where to find the respective
target description for each repository is implemented. For the given
example, the following `repos.json` defines the overlay
`"common-targets-layer"`, which is used by `"fmtlib"` and `"gsl-lite"`:

``` {.jsonc srcname="repos.gsl-lite.json"}
{ "main": "tutorial"
, "repositories":
  { "rules-cc":
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "3a5f0f0f50c59495ffc3b198df59e6edb8416450"
      , "repository": "https://github.com/just-buildsystem/rules-cc.git"
      , "subdir": "rules"
      }
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
  , "common-targets-layer":
    { "repository":
      { "type": "file"
      , "path": "./common-layer"
      , "pragma": {"to_git": true}
      }
    }
  , "fmtlib":
    { "repository":
      { "type": "git"
      , "branch": "8.1.1"
      , "commit": "b6f4ceaed0a0a24ccf575fab6c56dd50ccf6f1a9"
      , "repository": "https://github.com/fmtlib/fmt.git"
      }
    , "target_root": "common-targets-layer"
    , "target_file_name": "TARGETS.fmt"
    , "bindings": {"rules": "rules-cc"}
    }
  , "gsl-lite":
    { "repository":
      { "type": "git"
      , "branch": "v0.40.0"
      , "commit": "d6c8af99a1d95b3db36f26b4f22dc3bad89952de"
      , "repository": "https://github.com/gsl-lite/gsl-lite.git"
      }
    , "target_root": "common-targets-layer"
    , "target_file_name": "TARGETS.gsl"
    , "bindings": {"rules": "rules-cc"}
    }
  }
}
```

Using pre-built dependencies
----------------------------

While building external dependencies from source brings advantages, most
prominently the flexibility to quickly and seamlessly switch to a
different build configuration (production, debug, instrumented for
performance analysis; cross-compiling for a different target
architecture), there are also legitimate reasons to use pre-built
dependencies. The most prominent one is if your project is packaged as
part of a larger distribution. For that reason, just also has target files
for all its dependencies assuming
they are pre-installed. The reason why target files are used at all for
this situation is twofold.

 - On the one hand, having a target allows the remaining targets to not
   care about where their dependencies come from, or if it is a build
   against pre-installed dependencies or not. Also, the top-level
   binary does not have to know the linking requirements of its
   transitive dependencies. In other words, information stays where it
   belongs to and if one target acquires a new dependency, the
   information is automatically propagated to all targets using it.
 - Still some information is needed to use a pre-installed library and,
   as explained, a target describing the pre-installed library is the
   right place to collect this information.
    - The public header files of the library. By having this explicit,
      we do not accumulate directories in the include search path and
      hence also properly detect include conflicts.
    - The information on how to link the library itself (i.e.,
      basically its base name).
    - Any dependencies on other libraries that the library might have.
      This information is used to obtain the correct linking order and
      complete transitive linking arguments while keeping the
      description maintainable, as each target still only declares its
      direct dependencies.

A target description for a pre-built version of the format library
that was used as an example in this section is shown next; with our
staging mechanism the logical repository it belongs to is rooted in the
`fmt` subdirectory of the `include` directory of the ambient system.

``` {.jsonc srcname="etc/import.prebuilt/TARGETS.fmt"}
{ "fmt":
  { "type": ["@", "rules", "CC", "library"]
  , "name": ["fmt"]
  , "stage": ["fmt"]
  , "hdrs": [["TREE", null, "."]]
  , "private-ldflags": ["-lfmt"]
  }
}
```

However, even specifying all the include locations and headers can
be tedious and, in the end, it is information that `pkg-config` can
provide as well. So there is a rule to import libraries that way
and the actual packaging-build version of `libfmt`, as provided in
`etc/import.pkgconfig`, looks as follows.

``` {.jsonc srcname="etc/import.pkgconfig/TARGETS.fmt"}
{ "fmt":
  {"type": ["@", "rules", "CC/pkgconfig", "system_library"], "name": ["fmt"]}
}
```



[^1]: Explicit `TREE` references are always a list of length 3, to
      distinguish them from target references of length 2 (module and
      target name). Furthermore, the second list element is always `null`
      as we only want to allow tree references from the current module.
