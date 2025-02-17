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
    json.dump(targets, f)
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
INFO: Found 3 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","-p"]
INFO: Repository "src target tasks description" depends on 1 top-level computed roots
INFO: Requested target is [["@","src target tasks description","",""],{}]
INFO: Analysed target [["@","src target tasks description","",""],{}]
INFO: Export targets found: 0 cached, 1 uncached, 0 not eligible for caching
INFO: Discovered 1 actions, 0 trees, 0 blobs
INFO: Building [["@","src target tasks description","",""],{}].
INFO: Processed 1 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        TARGETS [254c72c511e84a84f42c92518d78c405f40ac5fe:462:f]
{"lib ./foo": {"type": "generic", "outs": ["TARGETS"], "deps": [["@", "utils", "", "generate-target.py"]], "cmds": ["./generate-target.py foo"]}, "lib ./bar": {"type": "generic", "outs": ["TARGETS"], "deps": [["@", "utils", "", "generate-target.py"], ["", "./bar/deps"]], "cmds": ["./generate-target.py bar ./bar/deps"]}, "stage": {"type": "install", "files": {"foo/TARGETS": "lib ./foo", "bar/TARGETS": "lib ./bar"}}, "": {"type": "export", "target": "stage"}}
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
      , "commit": "3a5f0f0f50c59495ffc3b198df59e6edb8416450"
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
INFO: Found 8 repositories to set up
INFO: Setup finished, exec ["just","analyse","-C","...","--log-limit","4","bar",""]
INFO: Repository "" depends on 1 top-level computed roots
PERF: Export target ["@","src target tasks description","",""] taken from cache: [7a9b0e386c9e3d3d185498876e52f9f52867af4e:120:f] -> [3d60470c815b923a7fe795e57281b715f52d2144:582:f]
PERF: Root [["@","src target tasks description","",""],{}] evaluated to 464a5693f8eb79507900ea5c9757508e790bf161, log cfd9aa252f1a61b098f2ffb4da19d00886787d5e
PERF: Export target ["@","src target build","",""] registered for caching: [89fb2fefeca243ed9ebc877e47d27db1bbed2920:120:f]
PERF: Root [["@","src target build","",""],{}] evaluated to db732bc9b76cb485970795dad3de7941567f4caa, log 048c8f45cc847ae95935d5132acee2651df00ffa
INFO: Requested target is [["@","","bar",""],{}]
INFO: Analysed target [["@","","bar",""],{}]
INFO: Result of target [["@","","bar",""],{}]: {
        "artifacts": {
          "bar/libbar.a": {"data":{"id":"13441397141cdf7beb6693406a378fb61fdc39882b6930000fb0f5159d188a0b","path":"work/bar/libbar.a"},"type":"ACTION"}
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
          "link-args": [
            "bar/libbar.a",
            "foo/libfoo.a"
          ],
          "link-deps": {
            "foo/libfoo.a": {"data":{"id":"8a14c7242e00dd3f38ab857d0f8c7b2fe128902ab5ebf65069e71bc2f9c4936f","path":"work/foo/libfoo.a"},"type":"ACTION"}
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

As this affects the target structure with commit those changes.

```shell
$ git add . && git commit -m 'New library baz'
```

After that, we can immediately build the new library.
```shell
$ just-mr build --log-limit 4 baz ''
INFO: Performing repositories setup
INFO: Found 8 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","--log-limit","4","baz",""]
INFO: Repository "" depends on 1 top-level computed roots
PERF: Export target ["@","src target tasks description","",""] registered for caching: [03ae0cf28c983014f5eb51685939d462eff595a1:120:f]
PERF: Root [["@","src target tasks description","",""],{}] evaluated to 6d689bfb84401c1bd1a58736e1f3438d75403892, log 715213ed3038987f18724559969e008550574ad6
PERF: Export target ["@","src target build","",""] registered for caching: [f5193f37b81bd335de68b6aad207c6e35fc9b16a:120:f]
PERF: Root [["@","src target build","",""],{}] evaluated to 739a4750d43328ad9c6ff7d9445246a6506368fd, log 7c5efb22ac00ec8ad6cb0ef93735cba24725e33c
INFO: Requested target is [["@","","baz",""],{}]
INFO: Analysed target [["@","","baz",""],{}]
INFO: Discovered 6 actions, 3 trees, 0 blobs
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
