Cross compiling and testing cross-compiled targets
==================================================

So far, we were always building for the platform on which we were
building. There are, however, good reasons to build on a different
one. For example, the other platform could be faster (common theme
when developing for embedded devices), cheaper, or simply available
in larger quantities. The standard solution for these kind of
situations is cross compiling: the binary is completely built on
one platform, while being intended to run on a different one.

Cross compiling using the CC rules
----------------------------------

The `C` and `C++` rules that come with the `justbuild` repository
already have cross-compiling already built into the tool-chain
definitions for the `"gnu"` and `"clang"` compiler families; as
different compilers expect different ways to be told about the target
architecture, cross compiling is not supported for the `"unknown"`
compiler family.

First, ensure that the required tools and libraries (which also have
to be available for the target architecture) for cross compiling
are installed. Depending on the distribution used, this can be
spread over several packages, often shared by the architecture to
cross compile to.

Once the required packages are installed, usage is quite forward.
Let's start a new project.
``` sh
$ touch ROOT
```

We create a file `repos.template.json` specifying the one local repository.
``` {.jsonc srcname="repos.template.json"}
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"rules": "rules-cc"}
    }
  }
}
```

The actual `rules-cc` are imported via `just-import-git`.
``` sh
$ just-import-git -C repos.template.json -b master --as rules-cc https://github.com/just-buildsystem/rules-cc > repos.json
```

Let's have `main.cpp` be the canonical Hello-World program.
``` {.cpp srcname="main.cpp"}
#include <iostream>

int main() {
  std::cout << "Hello world!\n";
  return 0;
}
```

Then, our `TARGETS` file describe a simple binary, as usual.
``` {.jsonc srcname="TARGETS"}
{ "helloworld":
  { "type": ["@", "rules", "CC", "binary"]
  , "name": ["helloworld"]
  , "srcs": ["main.cpp"]
  }
}
```

As mentioned in the introduction, we need to specify `COMPILER_FAMILY`,
`OS`, and `ARCH`. So the canonical building for host looks something like
the following.
``` sh
$ just-mr build -D '{"COMPILER_FAMILY": "gnu", "OS": "linux", "ARCH": "x86_64"}'
INFO: Performing repositories setup
INFO: Found 21 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","-D","{\"COMPILER_FAMILY\": \"gnu\", \"OS\": \"linux\", \"ARCH\": \"x86_64\"}"]
INFO: Requested target is [["@","","","helloworld"],{"ARCH":"x86_64","COMPILER_FAMILY":"gnu","OS":"linux"}]
INFO: Analysed target [["@","","","helloworld"],{"ARCH":"x86_64","COMPILER_FAMILY":"gnu","OS":"linux"}]
INFO: Discovered 2 actions, 1 trees, 0 blobs
INFO: Building [["@","","","helloworld"],{"ARCH":"x86_64","COMPILER_FAMILY":"gnu","OS":"linux"}].
INFO: Processed 2 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [0d5754a83c7c787b1c4dd717c8588ecef203fb72:16992:x]
$
```

To cross compile, we simply add `TARGET_ARCH`.
```
$ just-mr build -D '{"COMPILER_FAMILY": "gnu", "OS": "linux", "ARCH": "x86_64", "TARGET_ARCH": "arm64"}'
INFO: Performing repositories setup
INFO: Found 21 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","-D","{\"COMPILER_FAMILY\": \"gnu\", \"OS\": \"linux\", \"ARCH\": \"x86_64\", \"TARGET_ARCH\": \"arm64\"}"]
INFO: Requested target is [["@","","","helloworld"],{"ARCH":"x86_64","COMPILER_FAMILY":"gnu","OS":"linux","TARGET_ARCH":"arm64"}]
INFO: Analysed target [["@","","","helloworld"],{"ARCH":"x86_64","COMPILER_FAMILY":"gnu","OS":"linux","TARGET_ARCH":"arm64"}]
INFO: Discovered 2 actions, 1 trees, 0 blobs
INFO: Building [["@","","","helloworld"],{"ARCH":"x86_64","COMPILER_FAMILY":"gnu","OS":"linux","TARGET_ARCH":"arm64"}].
INFO: Processed 2 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [b45459ea3dd36c7531756a4de9aaefd6af30e417:9856:x]
$
```

To inspect the different command lines for native and cross compilation,
we can use `just analyse`.
``` sh
$ just-mr analyse -D '{"COMPILER_FAMILY": "gnu", "OS": "linux", "ARCH": "x86_64"}' --dump-actions -
$ just-mr analyse -D '{"COMPILER_FAMILY": "gnu", "OS": "linux", "ARCH": "x86_64", "TARGET_ARCH": "arm64"}' --dump-actions -
$ just-mr analyse -D '{"COMPILER_FAMILY": "clang", "OS": "linux", "ARCH": "x86_64"}' --dump-actions -
$ just-mr analyse -D '{"COMPILER_FAMILY": "clang", "OS": "linux", "ARCH": "x86_64", "TARGET_ARCH": "arm64"}' --dump-actions -
```


Testing in the presence of cross compiling
------------------------------------------

To understand testing the in the presence of cross compiling, let's
walk through a simple example. We create a basic test by adding
a file `test/TARGETS`.
``` {.jsonc srcname="test/TARGETS"}
{ "basic":
  { "type": ["@", "rules", "shell/test", "script"]
  , "name": ["basic"]
  , "test": ["basic.sh"]
  , "deps": [["", "helloworld"]]
  }
, "basic.sh":
  { "type": "file_gen"
  , "name": "basic.sh"
  , "data": "./helloworld | grep -i world"
  }
}
```

Now, if we try to run the test by simply specifying the target architecture,
we find that the binary to be tested is still only built for host.
``` sh
$ just-mr analyse --dump-targets - -D '{"COMPILER_FAMILY": "gnu", "OS": "linux", "ARCH": "x86_64", "TARGET_ARCH": "arm64"}' test basic
INFO: Performing repositories setup
INFO: Found 21 repositories to set up
INFO: Setup finished, exec ["just","analyse","-C","...","--dump-targets","-","-D","{\"COMPILER_FAMILY\": \"gnu\", \"OS\": \"linux\", \"ARCH\": \"x86_64\", \"TARGET_ARCH\": \"arm64\"}","test","basic"]
INFO: Requested target is [["@","","test","basic"],{"ARCH":"x86_64","COMPILER_FAMILY":"gnu","OS":"linux","TARGET_ARCH":"arm64"}]
INFO: Result of target [["@","","test","basic"],{"ARCH":"x86_64","COMPILER_FAMILY":"gnu","OS":"linux","TARGET_ARCH":"arm64"}]: {
        "artifacts": {
          "result": {"data":{"id":"33eb2ebd2ea0d6d335dfc1f948d14d506a19f693","path":"result"},"type":"ACTION"},
          "stderr": {"data":{"id":"33eb2ebd2ea0d6d335dfc1f948d14d506a19f693","path":"stderr"},"type":"ACTION"},
          "stdout": {"data":{"id":"33eb2ebd2ea0d6d335dfc1f948d14d506a19f693","path":"stdout"},"type":"ACTION"},
          "time-start": {"data":{"id":"33eb2ebd2ea0d6d335dfc1f948d14d506a19f693","path":"time-start"},"type":"ACTION"},
          "time-stop": {"data":{"id":"33eb2ebd2ea0d6d335dfc1f948d14d506a19f693","path":"time-stop"},"type":"ACTION"}
        },
        "provides": {
        },
        "runfiles": {
          "basic": {"data":{"id":"c57ed1d00bb6c800ff7ebd1c89519c8481885eda"},"type":"TREE"}
        }
      }
INFO: List of analysed targets:
{
  "@": {
    "": {
      "": {
        "helloworld": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"ADD_LDFLAGS":null,"ARCH":"x86_64","BUILD_POSITION_INDEPENDENT":null,"CC":null,"CFLAGS":null,"COMPILER_FAMILY":"gnu","CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"LDFLAGS":null,"OS":"linux","TARGET_ARCH":"x86_64"}]
      },
      "test": {
        "basic": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"ADD_LDFLAGS":null,"ARCH":"x86_64","ARCH_DISPATCH":null,"BUILD_POSITION_INDEPENDENT":null,"CC":null,"CFLAGS":null,"COMPILER_FAMILY":"gnu","CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"LDFLAGS":null,"OS":"linux","RUNS_PER_TEST":null,"TARGET_ARCH":"arm64","TEST_ENV":null,"TIMEOUT_SCALE":null}],
        "basic.sh": [{}]
      }
    },
    "rules-cc": {
      "CC": {
        "defaults": [{"ARCH":"x86_64","COMPILER_FAMILY":"gnu","DEBUG":null,"HOST_ARCH":null,"OS":"linux","TARGET_ARCH":"x86_64"}]
      }
    },
    "rules-cc/just/rules": {
      "CC": {
        "defaults": [{"ARCH":"x86_64","COMPILER_FAMILY":"gnu","DEBUG":null,"HOST_ARCH":null,"OS":"linux","TARGET_ARCH":"x86_64"}],
        "gcc": [{"ARCH":"x86_64","HOST_ARCH":null,"OS":"linux","TARGET_ARCH":"x86_64"}],
        "toolchain": [{"ARCH":"x86_64","COMPILER_FAMILY":"gnu","HOST_ARCH":null,"OS":"linux","TARGET_ARCH":"x86_64"}]
      }
    }
  }
}
INFO: Target tainted ["test"].
$
```

The reason is that a test actually has to run the created binary
and that requires a build environment of the target architecture.
So, if not being told how to obtain such an environment, they carry
out the test in the best manner they can, i.e., by transitioning
everything to host. So, in order to properly test the cross-compiled
binary, we need to do two things.

- We need to setup remote execution on the correct architecture,
  either by buying the appropriate hardware, or by running an emulator.
- We need to tell `justbuild` on how to reach that endpoint.

To continue the example, let's say we set up an `arm64` machine,
e.g., a Raspberry Pi, in the local network. On that machine, we can
simply run a single-node execution service using `just execute`;
note that the `just` binary used there has to be an `arm64` binary,
e.g., obtained by cross compiling.

The next step is to tell `justbuild` how to reach that machine;
as we only want to use it for certain actions we can't simply
set it as (default) remote-execution endpoint (specified by the
`-r` option). Instead we crate a file `dispatch.json`.

``` {.jsonc srcname="dispatch.json"}
[[{"runner": "arm64-worker"}, "10.204.62.67:9999"]]
```
This file contains a list of lists pairs (lists of length 2) with the
first entry a map of remote-execution properties and the second entry
a remote-execution endpoint. For each action, if the remote-execution
properties of that action match the first component of a pair the
second component of the first matching pair is taken as remote-execution
endpoint instead of the default endpoint specified on the command
line. Here "matching" means that all values in the domain of the
map have the same value in the remote-execution properties of the
action (in particular, `{}` matches everything); so more specific
dispatches have to be specified earlier in the list. In our case,
we have a single endpoint in our private network that we should
use whenever the property `"runner"` has value `"arm64-worker"`.
The IP/port specification might differ in your setup. The path to
this file is passed by the `--endpoint-configuration` option.

Next, we have to tell the test rule, that we actually do have a
runner for the `arm64` architecture. The rule expects this in the
`ARCH_DISPATCH` configuration variable that you might have seen in
the list of analysed targets earlier. It is expected to be a map
from architectures to remote-execution properties ensuring the action
will be run on that architecture. If the rules finds a non-empty
entry for the specified target architecture, it assumes a runner is
available and will run the test action with those properties; for
the compilation steps, still cross compiling is used. In our case,
we set `ARCH_DISPATCH` to `{"arm64": {"runner": "arm64-worker"}}`.

Finally, we have to provide the credentials needed for mutual
authentication with the remote-execution endpoint by setting `--tls-*`
options appropriately. `just` assumes that the same credentials
can be used all remote-execution endpoints involved. In our example
we're building locally (where the build process starts the actions
itself) which does not require any credentials; nevertheless, it
still accepts any credentials provided.

``` sh
$ just-mr build -D '{"COMPILER_FAMILY": "gnu", "OS": "linux", "ARCH": "x86_64", "TARGET_ARCH": "arm64", "ARCH_DISPATCH": {"arm64": {"runner": "arm64-worker"}}}' --endpoint-configuration dispatch.json --tls-ca-cert ca.crt --tls-client-cert client.crt --tls-client-key client.key test basic
INFO: Performing repositories setup
INFO: Found 21 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","-D","{\"COMPILER_FAMILY\": \"gnu\", \"OS\": \"linux\", \"ARCH\": \"x86_64\", \"TARGET_ARCH\": \"arm64\", \"ARCH_DISPATCH\": {\"arm64\": {\"runner\": \"arm64-worker\"}}}","--endpoint-configuration","dispatch.json","--tls-ca-cert","ca.crt","--tls-client-cert","client.crt","--tls-client-key","client.key","test","basic"]
INFO: Requested target is [["@","","test","basic"],{"ARCH":"x86_64","ARCH_DISPATCH":{"arm64":{"runner":"arm64-worker"}},"COMPILER_FAMILY":"gnu","OS":"linux","TARGET_ARCH":"arm64"}]
INFO: Analysed target [["@","","test","basic"],{"ARCH":"x86_64","ARCH_DISPATCH":{"arm64":{"runner":"arm64-worker"}},"COMPILER_FAMILY":"gnu","OS":"linux","TARGET_ARCH":"arm64"}]
INFO: Target tainted ["test"].
INFO: Discovered 3 actions, 3 trees, 1 blobs
INFO: Building [["@","","test","basic"],{"ARCH":"x86_64","ARCH_DISPATCH":{"arm64":{"runner":"arm64-worker"}},"COMPILER_FAMILY":"gnu","OS":"linux","TARGET_ARCH":"arm64"}].
INFO: Processed 3 actions, 2 cache hits.
INFO: Artifacts built, logical paths are:
        result [7ef22e9a431ad0272713b71fdc8794016c8ef12f:5:f]
        stderr [e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:0:f]
        stdout [cd0875583aabe89ee197ea133980a9085d08e497:13:f]
        time-start [298a717bc8ba9b7e681a6d789104a1204ebe75b8:11:f]
        time-stop [298a717bc8ba9b7e681a6d789104a1204ebe75b8:11:f]
      (1 runfiles omitted.)
INFO: Target tainted ["test"].
$
```

The resulting command line might look complicated, but the
authentication-related options, as well as the dispatch-related
options (including setting `ARGUMENTS_DISPATCH` via `-D`) can simply
be set in the `"just args"` entry of the `.just-mrrc` file.

When inspecting the result, we can use `just install-cas` as usual,
without any special arguments. Whenever dispatching an action to
a non-default end point, `just` will take care of syncing back the
artifacts to the default CAS.
