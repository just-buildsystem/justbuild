# Computed Roots

The general approach of writing a build description side-by-side with
the source code works in most cases. There are, however, cases where
the build description depends on the contents of source-like files.

Here we consider a somewhat contrieved example that, however, shows
all the various types of derived roots. Let's say we have a very
regular structure of our code base: one top-level directory for
each library and if there are depenedencies, then there is a plain
file `deps` listing, one entry per line, the libraries depended
upon. From that structure we want a derived build description that
is not maintained manually.

As an example, say, so far we have the file structure
```
src
 +--foo
 |    +-- foo.hpp
 |    +-- foo.cpp
 |
 +--bar
     +-- bar.hpp
     +-- bar.cpp
     +-- deps
```
where `src/bar/deps` contains a single line, saying `foo`.

The first step is to write a generator for a single `TARGETS` file. To clearly
separate the infrastructure files from the sources, we add the generator as
`utils/generate.py`.

```{.py srcname="utils/generate.py"}
#!/usr/bin/env python3

import json
import sys

name = sys.argv[1]

deps = []
if len(sys.argv) > 2:
    with open(sys.argv[2]) as f:
        deps = f.read().splitlines()

target = {"type": ["@", "rules", "CC", "library"],
          "name": [name],
          "hdrs": [["GLOB", None, "*.hpp"]],
          "srcs": [["GLOB", None, "*.cpp"]],
          "stage": [name],
          }
if deps:
    target["deps"] = [[x, ""] for x in deps]

targets = {"": target}

with open("TARGETS", "w") as f:
    json.dump(targets, f)
    f.write("\n")
```

A `TARGETS` file has to be created for every directory containing
files (and not just other directories). Additionally, there needs to be
a top-level target staging all those files that is exported. This can
be implemented by another script, say `utils/call-generator-targets.py`.

```{.py srcname="utils/call-generator-targets.py"}
#!/usr/bin/env python3

import json
import sys
import os

targets = {}
stage = {}

for root, dirs, files in os.walk("."):
    if files:
        target_name = "lib " + root
        with_deps = "deps" in files
        deps_name = os.path.join(root, "deps")
        entry = {"type": "generic",
                 "outs": ["TARGETS"],
                 "deps": ([["@", "utils", "", "generate.py"]]
                          + ([["", deps_name]] if with_deps else [])),
                 "cmds": ["./generate.py " + os.path.normpath(root)
                          + (" " + deps_name if with_deps else "")]}
        targets[target_name] = entry
        stage[os.path.normpath(os.path.join(root, "TARGETS"))] = target_name

targets["stage"] = {"type": "install", "files": stage}
targets[""] = {"type": "export", "target": "stage"}

with open(sys.argv[1], "w") as f:
    json.dump(targets, f, sort_keys=True)
    f.write("\n")
```

Of course, those scripts have to be executable.
```shell
$ chmod 755 utils/*.py
```

With that, we can generate the build description for generating
the target files. We first write a target file `utils/targets.generate`.

```{.json srcname="utils/targets.generate"}
{ "": {"type": "export", "target": "generate"}
, "generate":
  { "type": "generic"
  , "cmds": ["cd src && ../call-generator-targets.py ../TARGETS"]
  , "outs": ["TARGETS"]
  , "deps": [["@", "utils", "", "call-generator-targets.py"], "src"]
  }
, "src": {"type": "install", "dirs": [[["TREE", null, "."], "src"]]}
}
```

As we intend to make `utils` a separate logical repository, we also
add a trival top-level targets file.

```shell
$ echo {} > utils/TARGETS
```

Next we can start a repository description. Here we notice that
the tasks to be performed to generate the target files only depend
on the tree structure of the `src` repository. So, we use the
tree structure as workspace root to avoid unnecessary runs of
`utils/targets.generate`.

```{.json srcname="etc/repos.json"}
{ "repositories":
  { "src":
    {"repository": {"type": "file", "path": "src", "pragma": {"to_git": true}}}
  , "utils":
    { "repository":
      {"type": "file", "path": "utils", "pragma": {"to_git": true}}
    }
  , "src target tasks description":
    { "repository": {"type": "tree structure", "repo": "src"}
    , "target_root": "utils"
    , "target_file_name": "targets.generate"
    , "bindings": {"utils": "utils"}
    }
  }
}
```

Of course, the `"to_git"` pragma works best, if we have everything under version
control (which is a good idea in general anyway).
```shell
$ git init
$ git add .
$ git commit -m 'Initial commit'
```

Now the default target of `"src target tasks description"` shows how to
build the target files we want.

```shell
$ just-mr --main 'src target tasks description' build -p
INFO: Performing repositories setup
INFO: Found 3 repositories involved
INFO: Setup finished, exec ["just","build","-C","...","-p"]
INFO: Repository "src target tasks description" depends on 1 top-level computed roots
INFO: Requested target is [["@","src target tasks description","",""],{}]
INFO: Analysed target [["@","src target tasks description","",""],{}]
INFO: Export targets found: 0 cached, 1 uncached, 0 not eligible for caching
INFO: Discovered 1 actions, 0 tree overlays, 0 trees, 0 blobs
INFO: Building [["@","src target tasks description","",""],{}].
INFO: Processed 1 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        TARGETS [68336b9823a86d0f64a5a79990c6b171d4f6523b:434:f]
{"": {"target": "stage", "type": "export"}, "lib ./bar": {"cmds": ["./generate.py bar ./bar/deps"], "deps": [["@", "utils", "", "generate.py"], ["", "./bar/deps"]], "outs": ["TARGETS"], "type": "generic"}, "lib ./foo": {"cmds": ["./generate.py foo"], "deps": [["@", "utils", "", "generate.py"]], "outs": ["TARGETS"], "type": "generic"}, "stage": {"files": {"bar/TARGETS": "lib ./bar", "foo/TARGETS": "lib ./foo"}, "type": "install"}}
INFO: Backing up artifacts of 1 export targets
```

From that, we can, step by step, define the actual build description.

 - The tasks to generate the target files is a computed root of the
   `"src target tasks"` using the top-level target `["", ""]`. We
   call it `"src target tasks"`.
 - This root will be the target root in the repository `src target
   build` describing how to generate the actual target files.
 - The `"src targets"` are then, again, a computed root.
 - Finally, we can define the top-level repository `""`. As we want
   to be able to build with uncommitted changes as long as they
   do not affect the target description, we use an explicit file
   repository instead of referring to the `to_git` repository `"src"`.
 - As the top-level targets also depend on our C/C++ rules, we
   include those as well and set an appropriate binding for `""`.

Therefore, our final repository description looks as follows.
```{.json srcname="etc/repos.json"}
{ "repositories":
  { "src":
    {"repository": {"type": "file", "path": "src", "pragma": {"to_git": true}}}
  , "utils":
    { "repository":
      {"type": "file", "path": "utils", "pragma": {"to_git": true}}
    }
  , "src target tasks description":
    { "repository": {"type": "tree structure", "repo": "src"}
    , "target_root": "utils"
    , "target_file_name": "targets.generate"
    , "bindings": {"utils": "utils"}
    }
  , "src target tasks":
    { "repository":
      { "type": "computed"
      , "repo": "src target tasks description"
      , "target": ["", ""]
      }
    }
  , "src target build":
    { "repository": "src"
    , "target_root": "src target tasks"
    , "bindings": {"utils": "utils"}
    }
  , "src targets":
    { "repository":
      {"type": "computed", "repo": "src target build", "target": ["", ""]}
    }
  , "":
    { "repository": {"type": "file", "path": "src"}
    , "target_root": "src targets"
    , "bindings": {"rules": "rules"}
    }
  , "rules":
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "7a2fb9f639a61cf7b7d7e45c7c4cea845e7528c6"
      , "repository": "https://github.com/just-buildsystem/rules-cc.git"
      , "subdir": "rules"
      }
    }
  }
}
```

With that, we can now analyse `["bar", ""]` and see that the dependency we
wrote in `src/bar/deps` is honored. With increased log level we can also see
hints on the computation of the computed roots.

```shell
$ just-mr analyse --log-limit 4 bar ''
INFO: Performing repositories setup
INFO: Found 8 repositories involved
INFO: Setup finished, exec ["just","analyse","-C","...","--log-limit","4","bar",""]
INFO: Repository "" depends on 1 top-level computed roots
PERF: Export target ["@","src target tasks description","",""] taken from cache: [52dd0203238644382280dc9ae79c75d1f7e5adf1:120:f] -> [79a4114597a03d82acfd95a5f95f0d22c6f09ccb:582:f]
PERF: Root [["@","src target tasks description","",""],{}] evaluated to e5dfc10a073e3e101a256bc38fae67ec234afccb, log aa803f1f445bfe20826495e33252ea02c4c1d7e0
PERF: Export target ["@","src target build","",""] registered for caching: [309aac8800c83359aa900b7368b25b03bb343110:120:f]
PERF: Root [["@","src target build","",""],{}] evaluated to db732bc9b76cb485970795dad3de7941567f4caa, log c33eb0167ccd7b5b3b8db51657d0d80885c61f98
INFO: Requested target is [["@","","bar",""],{}]
INFO: Analysed target [["@","","bar",""],{}]
INFO: Result of target [["@","","bar",""],{}]: {
        "artifacts": {
          "bar/libbar.a": {"data":{"id":"081122c668771bb09ef30b12687c6f131583506714a992595133ab9983366ce7","path":"work/bar/libbar.a"},"type":"ACTION"}
        },
        "provides": {
          "compile-args": [
          ],
          "compile-deps": {
            "foo/foo.hpp": {"data":{"path":"foo/foo.hpp","repository":""},"type":"LOCAL"}
          },
          "debug-hdrs": {
          },
          "debug-srcs": {
          },
          "dwarf-pkg": {
          },
          "link-args": [
            "bar/libbar.a",
            "foo/libfoo.a"
          ],
          "link-deps": {
            "foo/libfoo.a": {"data":{"id":"613a6756639b7fac44a698379581f7ac9113536f95722e4180cae3af45befeb9","path":"work/foo/libfoo.a"},"type":"ACTION"}
          },
          "lint": [
          ],
          "package": {
            "cflags-files": {},
            "ldflags-files": {},
            "name": "bar"
          },
          "run-libs": {
          },
          "run-libs-args": [
          ]
        },
        "runfiles": {
          "bar/bar.hpp": {"data":{"path":"bar/bar.hpp","repository":""},"type":"LOCAL"}
        }
      }
```

The quoted logs can be inspected with the `install-cas` subcommand as usual.

To see how target files adapt to source changes, let's
add a new directory `baz` with source and header files,
as well as a `deps` file saying `bar`.

```shell
$ mkdir src/baz
$ echo '#include "bar/bar.hpp" #...' > src/baz/baz.hpp
$ touch src/baz/baz.cpp
$ echo 'bar' > src/baz/deps
```

As this affects the target structure, we commit those changes.

```shell
$ git add . && git commit -m 'New library baz'
```

After that, we can immediately build the new library.
```shell
$ just-mr build --log-limit 4 baz ''
INFO: Performing repositories setup
INFO: Found 8 repositories involved
INFO: Setup finished, exec ["just","build","-C","...","--log-limit","4","baz",""]
INFO: Repository "" depends on 1 top-level computed roots
PERF: Export target ["@","src target tasks description","",""] registered for caching: [02e0545e14758f7fe08a90b56cbfae2e12bdd51e:120:f]
PERF: Root [["@","src target tasks description","",""],{}] evaluated to 43a0b068d6065519061b508a22725c50e68279be, log bf4cc2d0d803bcff78bd7e4e835440467f3a3674
PERF: Export target ["@","src target build","",""] registered for caching: [00c234393fc6986c308c16d6847ed09e79282097:120:f]
PERF: Root [["@","src target build","",""],{}] evaluated to 739a4750d43328ad9c6ff7d9445246a6506368fd, log 51525391c622a398b5190adee07c9de6338d56ba
INFO: Requested target is [["@","","baz",""],{}]
INFO: Analysed target [["@","","baz",""],{}]
INFO: Discovered 6 actions, 0 tree overlays, 3 trees, 0 blobs
INFO: Building [["@","","baz",""],{}].
INFO: Processed 2 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        baz/libbaz.a [c6eb3219ec0b1017f242889327f9c2f93a316546:1060:f]
      (1 runfiles omitted.)
INFO: Target tainted ["test"].
```

Obviously, the tree structure has changed, so `"src target tasks
decription"` target gets rebuild. Also, the `"src target build"`
target gets rebuild, but if we inspect the log, we see that 2 out
of 3 actions are taken from cache.

A similar construction is also used in the `justbuild` main `git`
repository for describing the task of formatting all JSON files: the
target root of the logical repository `"format-json"` is computed,
based on the underlying tree structure. Again, the workspace root
for `"format-json"` is the plain file root, so that uncommitted
changes (to committed files) are taken into account.
