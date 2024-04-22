Ensuring reproducibility of the build
=====================================

Software builds should be
[reproducible](https://reproducible-builds.org/). *Justbuild*
supports this goal in local builds by isolating individual actions,
setting permissions and file time stamps to canonical values, etc.; most
remote execution systems take even further measures to ensure the
environment always looks the same to every action. Nevertheless, it is
always possible to break reproducibility by bad actions, both coming
from rules not carefully written, as well as from ad-hoc actions added
by the `generic` target, such as

``` jsonc
...
, "version.h":
  { "type": "generic"
  , "cmds":
    ["echo '#define VERSION \"0.0.0.'`date +%Y%m%d%H%M%S`'\"' > version.h"]
  , "outs": ["version.h"]
  }
...
```

Besides time stamps there are many other sources of nondeterminism, like
properties of the build machine (name, number of CPUs available, etc),
but also subtle ones like `readdir` order. Often, those non-reproducible
parts get buried deeply in a final artifact (like the version string
embedded in a binary contained in a compressed installation archive);
and, as long as the non-reproducible action stays in cache, it does not
even result in bad incrementality. Still, others won't be able to
reproduce the exact artifact.

There are tools like [diffoscope](https://diffoscope.org/) to deeply
compare archives and other container formats. Nevertheless, it is
desirable to find the root causes, i.e., the first (in topological
order) actions that yield a different output.

Rebuilding
----------

For the remainder of this section, we will consider the following
example project with the C++ source file `hello.cpp`:

``` {.cpp srcname="hello.cpp"}
#include <iostream>
#include "version.h"

int main(int argc, const char* argv[]) {
    if (argc > 1 && std::string{argv[1]} == "-v") {
        std::cout << VERSION << std::endl;
    }
    return 0;
}
```

and the following `TARGETS` file:

``` {.jsonc srcname="TARGETS"}
{ "":
  { "type": "install"
  , "files":
    { "bin/hello": "hello"
    , "share/hello/version.txt": "version.txt"
    , "share/hello/OUT.txt": "OUT.txt"
    }
  }
, "hello":
  { "type": ["@", "rules", "CC", "binary"]
  , "name": ["hello"]
  , "srcs": ["hello.cpp"]
  , "private-hdrs": ["version.h"]
  }
, "version.h":
  { "type": "generic"
  , "cmds":
    ["echo '#define VERSION \"0.0.0.'`date +%Y%m%d%H%M%S`'\"' > version.h"]
  , "outs": ["version.h"]
  }
, "version.txt":
  { "type": "generic"
  , "outs": ["version.txt"]
  , "cmds": ["./hello -v > version.txt"]
  , "deps": ["hello"]
  }
, "out.txt":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["./hello > out.txt"]
  , "deps": ["hello"]
  }
, "OUT.txt":
  { "type": "generic"
  , "outs": ["OUT.txt"]
  , "cmds": ["tr a-z A-Z > OUT.txt < out.txt"]
  , "deps": ["out.txt"]
  }
}
```

The `repos.json` only needs the `"rules-cc"` repository and as main repository
the current working directory

``` {.jsonc srcname="repos.json"}
{ "main": ""
, "repositories":
  { "rules-cc":
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "307c96681e6626286804c45273082dff94127878"
      , "repository": "https://github.com/just-buildsystem/rules-cc.git"
      , "subdir": "rules"
      }
    }
  , "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"rules": "rules-cc"}
    }
  }
}
```

To search for the root cause of non-reproducibility, `just` has a
subcommand `rebuild`. It builds the specified target again, requesting
that every action be executed again (but target-level cache is still
active); then the result of every action is compared to the one in the
action cache, if present with the same inputs. So, you typically would
first `build` and then `rebuild`. Note that a repeated `build` simply
takes the action result from cache.

``` sh
$ touch ROOT
$ just-mr build
INFO: Performing repositories setup
INFO: Found 2 repositories to set up
INFO: Setup finished, exec ["just","build","-C","..."]
INFO: Requested target is [["@","","",""],{}]
INFO: Analysed target [["@","","",""],{}]
INFO: Discovered 6 actions, 1 trees, 0 blobs
INFO: Building [["@","","",""],{}].
INFO: Processed 6 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        bin/hello [68528cc2b32838573582c4dd4f0eccbae04cb5d6:18072:x]
        share/hello/OUT.txt [e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:0:f]
        share/hello/version.txt [8a11b38c825a23d8a0ad36ba6793ded347c0aec8:21:f]
$ just-mr build
INFO: Performing repositories setup
INFO: Found 2 repositories to set up
INFO: Setup finished, exec ["just","build","-C","..."]
INFO: Requested target is [["@","","",""],{}]
INFO: Analysed target [["@","","",""],{}]
INFO: Discovered 6 actions, 1 trees, 0 blobs
INFO: Building [["@","","",""],{}].
INFO: Processed 6 actions, 6 cache hits.
INFO: Artifacts built, logical paths are:
        bin/hello [68528cc2b32838573582c4dd4f0eccbae04cb5d6:18072:x]
        share/hello/OUT.txt [e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:0:f]
        share/hello/version.txt [8a11b38c825a23d8a0ad36ba6793ded347c0aec8:21:f]
$ just-mr rebuild
INFO: Performing repositories setup
INFO: Found 2 repositories to set up
INFO: Setup finished, exec ["just","rebuild","-C","..."]
INFO: Requested target is [["@","","",""],{}]
INFO: Analysed target [["@","","",""],{}]
INFO: Discovered 6 actions, 1 trees, 0 blobs
INFO: Rebuilding [["@","","",""],{}].
WARN: Found flaky action:
       - id: 98a14505f43f623c8154e29d4b2ae791bfee718a
       - cmd: ["sh","-c","echo '#define VERSION \"0.0.0.'`date +%Y%m%d%H%M%S`'\"' > version.h\n"]
       - output 'version.h' differs:
         - [725d7c207138ed941a181fd2c068a06dd60dff6e:39:f] (rebuilt)
         - [c1a2986953451f77021e121c15947259e33dd645:39:f] (cached)
INFO: 2 actions compared with cache, 1 flaky actions found (0 of which tainted), no cache entry found for 4 actions.
INFO: Artifacts built, logical paths are:
        bin/hello [b268a81d3192af9ef737ebd746f941f441dbc3db:18072:x]
        share/hello/OUT.txt [e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:0:f]
        share/hello/version.txt [202ca71fec8bc5878559261a50c4547e0c851fb1:21:f]
$
```

In the example, the second action compared to cache is the upper casing
of the output. Even though the generation of `out.txt` depends on the
non-reproducible `hello`, the file itself is reproducible. Therefore,
the follow-up actions are checked as well.

For this simple example, reading the console output is enough to
understand what's going on. However, checking for reproducibility
usually is part of a larger, quality-assurance process. To support the
automation of such processes, the findings can also be reported in
machine-readable form.

``` sh
$ just-mr rebuild --dump-flaky flakes.json --dump-graph actions.json
[...]
$ cat flakes.json
{
  "cache misses": [
    "e346c2c18fe1f4662f772f12e85af968ecb2467c",
    "2655bfeee10cdefb61c608fe8e81ce3f2846ab0f",
    "108c6dee4f87b86cbfdd3cb8c74079a27444f846",
    "14a72ddc8f4024eafb290815bc725e1a0d02c065"
  ],
  "flaky actions": {
    "98a14505f43f623c8154e29d4b2ae791bfee718a": {
      "version.h": {
        "cached": {
          "file_type": "f",
          "id": "c1a2986953451f77021e121c15947259e33dd645",
          "size": 39
        },
        "rebuilt": {
          "file_type": "f",
          "id": "52996c0d65266d15173c7c2836b1835070c1a934",
          "size": 39
        }
      }
    }
  }
}$
```

The file reports the flaky actions together with the non-reproducible
artifacts they generated, reporting both, the cached and the newly
generated output. The files themselves can be obtained via `just
install-cas` as usual, allowing deeper comparison of the outputs. The
full definitions of the actions can be found in the action graph, in the
example dumped as well as `actions.json`; this definition also includes
the origins for each action, i.e., the configured targets that requested
the respective action.

Comparing build environments
----------------------------

Simply rebuilding on the same machine is good way to detect embedded
time stamps of sufficiently small granularity; for other sources of
non-reproducibility, however, more modifications of the environment are
necessary.

A simple, but effective, way for modifying the build environment is the
option `-L` to set the local launcher, a list of strings the argument
vector is prefixed with before the action is executed. The default
`["env", "--"]` simply resolves the program to be executed in the
current value of `PATH`, but a different value for the launcher can
obviously be used to set environment variables like `LD_PRELOAD`.
Relevant libraries and tools include
[libfaketime](https://github.com/wolfcw/libfaketime),
[fakehostname](https://github.com/dtcooper/fakehostname), and
[disorderfs](https://salsa.debian.org/reproducible-builds/disorderfs).

More variation can be achieved by comparing remote execution builds,
either for two different remote-execution end points or comparing one
remote-execution end point to the local build. The latter is also a good
way to find out where a build that "works on my machine" differs. The
endpoint on which the rebuild is executed can be set, in the same way as
for build with the `-r` option; the cache end point to compare against
can be set via the `--vs` option.
