# Running Linters

It is common to run some form of linters over the code base. It is
desirable to also use our build tool for this to have the benefit
of parallel (or even remote) build and sound caching. Additionally,
this also allows the lint tools to see the file layout as it occurs
in the actual compile action, including generated files. Remember
that even for source files this layout does not have to coincide
with the layout of files in the source repository.

Conveniently, our build rules have support for collecting the
relevant information needed for linting built in. If a target is
built with the configuration variable `LINT` set to a true value, lint
information is provided for the transitive sources;
as [third-party dependencies](third-party-software.md) are typically
exported without `LINT` among the flexible variables, that naturally
forms a boundary of the "own" code (to be linted, as opposed to
third-party code). So, continuing the
[third-party tutorial](third-party-software.md), we can obtain
abstract nodes for our sources (`main.cpp`, `greet/greet.hpp`,
`greet/greet.cpp`).

``` sh
$ just-mr analyse -D '{"LINT": true}' --dump-nodes -
INFO: Performing repositories setup
INFO: Found 5 repositories involved
INFO: Setup finished, exec ["just","analyse","-C","...","-D","{\"LINT\": true}","--dump-nodes","-"]
INFO: Requested target is [["@","tutorial","","helloworld"],{"LINT":true}]
INFO: Analysed target [["@","tutorial","","helloworld"],{"LINT":true}]
INFO: Export targets found: 0 cached, 1 uncached, 0 not eligible for caching
INFO: Result of target [["@","tutorial","","helloworld"],{"LINT":true}]: {
        "artifacts": {
          "helloworld": {"data":{"id":"1154ef311dff82653bc6a1a92bfc6152bc116cb86652b4b5218385fe39054391","path":"work/helloworld"},"type":"ACTION"}
        },
        "provides": {
          "debug-hdrs": {
          },
          "debug-srcs": {
          },
          "dwarf-pkg": {
          },
          "lint": [
            {"id":"aec2651a9f3b869554e3a2e5cc3ab85d86610a79aae388a4f7839396dc2167d2","type":"NODE"},
            {"id":"509e506f8d0c2ebe4fca63fe7cc528be17165bae464410ac5fba97a6ed92930d","type":"NODE"},
            {"id":"cb3bd5f3934e199ddf5d43eae6866f8c6ba449f060213a244c93f0826c070c3f","type":"NODE"}
          ],
          "package": {
            "to_bin": true
          },
          "run-libs": {
          }
        },
        "runfiles": {
        }
      }
INFO: Target nodes of target [["@","tutorial","","helloworld"],{"LINT":true}]:
{
  "509e506f8d0c2ebe4fca63fe7cc528be17165bae464410ac5fba97a6ed92930d": {
    "result": {
      "artifact_stage": {
        "include": {
          "data": {
            "id": "a882ca13d51c70aa6b02d4996aa426ee6e21bf85881ec0f6dbe0a278f5a27b7b"
          },
          "type": "TREE"
        },
        "work/greet/greet.hpp": {
          "data": {
            "path": "greet/greet.hpp",
            "repository": "tutorial"
          },
          "type": "LOCAL"
        }
      },
      "provides": {
        "cmd": ["c++","-O2","-Wall","-I","work","-isystem","include","-E","work/greet/greet.hpp"],
        "direct deps artifact names": ["include/fmt","work/greet/greet.hpp"],
        "extra outs": [],
        "src": "work/greet/greet.hpp"
      },
      "runfiles": {
      }
    },
    "type": "VALUE_NODE"
  },
  "aec2651a9f3b869554e3a2e5cc3ab85d86610a79aae388a4f7839396dc2167d2": {
    "result": {
      "artifact_stage": {
        "include": {
          "data": {
            "id": "29d6c7fef3c48a1a3b15edd770b33d073d5c9cd1e6e9fb22917831fcdd762ebb"
          },
          "type": "TREE"
        },
        "work/main.cpp": {
          "data": {
            "path": "main.cpp",
            "repository": "tutorial"
          },
          "type": "LOCAL"
        }
      },
      "provides": {
        "cmd": ["c++","-O2","-Wall","-I","work","-isystem","include","-c","work/main.cpp","-o","work/main.o"],
        "direct deps artifact names": ["include/greet/greet.hpp"],
        "extra outs": [],
        "src": "work/main.cpp"
      },
      "runfiles": {
      }
    },
    "type": "VALUE_NODE"
  },
  "cb3bd5f3934e199ddf5d43eae6866f8c6ba449f060213a244c93f0826c070c3f": {
    "result": {
      "artifact_stage": {
        "include": {
          "data": {
            "id": "a882ca13d51c70aa6b02d4996aa426ee6e21bf85881ec0f6dbe0a278f5a27b7b"
          },
          "type": "TREE"
        },
        "work/greet/greet.cpp": {
          "data": {
            "path": "greet/greet.cpp",
            "repository": "tutorial"
          },
          "type": "LOCAL"
        },
        "work/greet/greet.hpp": {
          "data": {
            "path": "greet/greet.hpp",
            "repository": "tutorial"
          },
          "type": "LOCAL"
        }
      },
      "provides": {
        "cmd": ["c++","-O2","-Wall","-I","work","-isystem","include","-c","work/greet/greet.cpp","-o","work/greet/greet.o"],
        "direct deps artifact names": ["include/fmt","work/greet/greet.hpp"],
        "extra outs": [],
        "src": "work/greet/greet.cpp"
      },
      "runfiles": {
      }
    },
    "type": "VALUE_NODE"
  }
}
```

We find the sources in correct staging, together with the respective
compile command (or preprocessing, in case of headers) provided.
The latter is important, to find the correct include files and to
know the correct defines to be used.

Of course, those abstract nodes are just an implementation detail
and there is a rule to define linting for the collected sources.
It takes two programs (targets consisting of a single artifact),

- the `linter` for running the lint task on a single file, and
- the `summarizer` for summarizing the lint results;

additionally, arbitrary `config` data can be given to have config
files available, but also to use a linter built from source.

As for every rule, the details can be obtained with the `describe`
subcommand.

``` sh
$ just-mr --main rules-cc describe --rule lint targets
INFO: Performing repositories setup
INFO: Found 2 repositories involved
INFO: Setup finished, exec ["just","describe","-C","...","--rule","lint","targets"]
 | Run a given linter on the lint information provided by the given targets.
...
 Target fields
 - "linter"
   | Single artifact running the lint checks.
   |
   | This program is invoked with
...
```

Let's go through these programs we have to provide one by one. The
first one is supposed to call the actual linter; as many linters,
including `clang-tidy` which we use as an example, prefer to obtain
the command information through a
[compilation database](https://clang.llvm.org/docs/JSONCompilationDatabase.html)
there is actually some work to do, especially as the directory entry
has to be an absolute path. We also move the configuration file
`.clang-tidy` from the configuration directory (located in a directory given
to us through the environment variable `CONFIG`) to the position
expected by `clang-tidy`.

``` {.python srcname="run_clang_tidy.py"}
#!/usr/bin/env python3

import json
import os
import shutil
import subprocess
import sys

def dump_meta(src, cmd):
    OUT = os.environ.get("OUT")
    with open(os.path.join(OUT, "config.json"), "w") as f:
        json.dump({"src": src, "cmd": cmd}, f)

def run_lint(src, cmd):
    dump_meta(src, cmd)
    config = os.environ.get("CONFIG")
    shutil.copyfile(os.path.join(config, ".clang-tidy"),
                    ".clang-tidy")
    db = [ {"directory": os.getcwd(),
            "arguments": cmd,
            "file": src}]
    with open("compile_commands.json", "w") as f:
        json.dump(db,f)
    new_cmd = [ "clang-tidy", src ]
    return subprocess.run(new_cmd).returncode

if __name__ == "__main__":
    sys.exit(run_lint(sys.argv[1], sys.argv[2:]))
```

The actual information on success or failure is provided through
the exit code and information on the problems discovered (if any)
is reported on stdout or stderr. Additionally, our launcher also
writes the meta data in a file `config.json` in the directory for
additional (usually machine-readable) diagnose output; the location
of this directory is given to us by the environment variable `OUT`.

We use a pretty simple `.clang-tidy` for demonstration purpose.

``` {.md srcname=".clang-tidy"}
Checks: 'clang-analyzer-*,misc-*,-misc-include-*'
WarningsAsErrors: 'clang-analyzer-*,misc-*,-misc-include-*'
```

Computing a summary of the individual lint results (given to the
summarizer as subdirectories of the current working directory) is
straight forward: the overall linting passed if all individual checks
passed and for the failed tests we format stdout and stderr in some
easy-to-read way; additionally, we also provide a machine-readable
summary of the failures.

``` {.py srcname="summary.py"}
#!/usr/bin/env python3

import json
import os
import sys

FAILED = {}

for lint in sorted(os.listdir()):
    if os.path.isdir(lint):
        with open(os.path.join(lint, "result")) as f:
            result = f.read().strip()
        if result != "PASS":
            record = {}
            with open(os.path.join(lint, "out/config.json")) as f:
                record["config"] = json.load(f)
            with open(os.path.join(lint, "stdout")) as f:
                log = f.read()
            with open(os.path.join(lint, "stderr")) as f:
                log += f.read()
            record["log"] = log
            FAILED[lint] = record

with open(os.path.join(os.environ.get("OUT"), "failures.json"), "w") as f:
    json.dump(FAILED, f)

failures = list(FAILED.keys())

for f in failures:
    src = FAILED[f]["config"]["src"]
    log = FAILED[f]["log"]

    print("%s %s" % (f, src))
    print("".join(["    " + line + "\n"
                   for line in log.splitlines()]))


if failures:
    sys.exit(1)
```

Of course, our launcher and summarizer have to be executable

``` sh
$ chmod 755 run_clang_tidy.py summary.py
```

Now we can define our lint target.

``` {.jsonc srcname="TARGETS"}
...
, "lint":
  { "type": ["@", "rules", "lint", "targets"]
  , "targets": ["helloworld"]
  , "linter": ["run_clang_tidy.py"]
  , "summarizer": ["summary.py"]
  , "config": [".clang-tidy"]
  }
...
```

As most rules, the lint rules also have a `"defaults"` target,
which allows to set `PATH` appropriately for all lint actions.
This can be useful if the linters are installed in a non-standard
directory.

``` sh
$ mkdir -p tutorial-defaults/lint
$ echo '{"defaults": {"type": "defaults", "PATH": ["'"${TOOLCHAIN_PATH}"'"]}}' > tutorial-defaults/lint/TARGETS
$ git add tutorial-defaults
$ git commit -m 'add lint defaults'
```

We now can build our lint report in the same way as any test report.

``` sh
$ just-mr build lint -P report
INFO: Performing repositories setup
INFO: Found 5 repositories involved
INFO: Setup finished, exec ["just","build","-C","...","lint","-P","report"]
INFO: Requested target is [["@","tutorial","","lint"],{}]
INFO: Analysed target [["@","tutorial","","lint"],{}]
INFO: Export targets found: 0 cached, 1 uncached, 0 not eligible for caching
INFO: Target tainted ["lint"].
INFO: Discovered 11 actions, 0 tree overlays, 7 trees, 0 blobs
INFO: Building [["@","tutorial","","lint"],{}].
INFO: Processed 7 actions, 3 cache hits.
INFO: Artifacts built, logical paths are:
        out [a90a9e3a8ac23526eb31ae46c80434cfd5810ed5:41:t]
        report [e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:0:f]
        result [7ef22e9a431ad0272713b71fdc8794016c8ef12f:5:f]
        work [52b9cfc07b53c59fb066bc95329f4ca6457e7338:111:t]
INFO: Backing up artifacts of 1 export targets
INFO: Target tainted ["lint"].
```

To see that some real linting is going on, let's modify one
of our source files. Say, we'll make the greeting independent
of the recipient.

``` {.cpp srcname="greet/greet.cpp"}
#include "greet.hpp"
#include <fmt/format.h>

void greet(std::string const& s) {
  fmt::print("Hello!\n");
}
```

Building succeeds without any warning.

``` sh
$ just-mr build helloworld
INFO: Performing repositories setup
INFO: Found 5 repositories involved
INFO: Setup finished, exec ["just","build","-C","...","helloworld"]
INFO: Requested target is [["@","tutorial","","helloworld"],{}]
INFO: Analysed target [["@","tutorial","","helloworld"],{}]
INFO: Export targets found: 1 cached, 0 uncached, 0 not eligible for caching
INFO: Discovered 4 actions, 0 tree overlays, 2 trees, 0 blobs
INFO: Building [["@","tutorial","","helloworld"],{}].
INFO: Processed 4 actions, 1 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [2cb87c743e9fd3d18543732945df3ef9ca084be6:132736:x]
```

However, the linter reports it.
``` sh
$ just-mr build lint -P report || :
INFO: Performing repositories setup
INFO: Found 5 repositories involved
INFO: Setup finished, exec ["just","build","-C","...","lint","-P","report"]
INFO: Requested target is [["@","tutorial","","lint"],{}]
INFO: Analysed target [["@","tutorial","","lint"],{}]
INFO: Export targets found: 1 cached, 0 uncached, 0 not eligible for caching
INFO: Target tainted ["lint"].
INFO: Discovered 8 actions, 0 tree overlays, 6 trees, 0 blobs
INFO: Building [["@","tutorial","","lint"],{}].
WARN (action:415a94f0c74ec937e2504b9ef5f94696232ff2c57eb2bec00c226896e2eb8be6):
     lint failed for work/greet/greet.cpp (exit code 1); outputs:
      - "result" [94e1707e853c36f514de3876408c09a0e0ca6fc4:5:f]
      - "stderr" [ffc377e8898697782ab96419f6ab82c60985c752:235:f]
      - "stdout" [17975e013bd2cc3f66509e11737e1e169f1bd162:231:f]
      - "out" [caf25f0a518d21909625f9a7974002796f6d8b5f:39:t]
INFO: Processed 4 actions, 2 cache hits.
INFO: Artifacts built, logical paths are:
        out [c298959107421711f8d87a2b96e95858c065b9b9:41:t] FAILED
        report [0b0ab9eb90c28ece0f14a13a6ae5c97da4a32170:531:f] FAILED
        result [94e1707e853c36f514de3876408c09a0e0ca6fc4:5:f] FAILED
        work [007eec6bad8b691c067dd2c54165ac2912711474:111:t] FAILED
INFO: Failed artifacts:
        out [c298959107421711f8d87a2b96e95858c065b9b9:41:t] FAILED
        report [0b0ab9eb90c28ece0f14a13a6ae5c97da4a32170:531:f] FAILED
        result [94e1707e853c36f514de3876408c09a0e0ca6fc4:5:f] FAILED
        work [007eec6bad8b691c067dd2c54165ac2912711474:111:t] FAILED
0000000002 work/greet/greet.cpp
    work/greet/greet.cpp:4:31: error: parameter 's' is unused [misc-unused-parameters,-warnings-as-errors]
        4 | void greet(std::string const& s) {
          |                               ^
          |                                /*s*/
    287 warnings generated.
    Suppressed 286 warnings (286 in non-user code).
    Use -header-filter=.* to display errors from all non-system headers. Use -system-headers to display errors from system headers as well.
    1 warning treated as error

INFO: Target tainted ["lint"].
WARN: Build result contains failed artifacts.
```
