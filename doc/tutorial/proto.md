Using protocol buffers
======================

The rules *justbuild* uses for itself also support protocol buffers.
This tutorial shows how to use those rules and the targets associated
with them. It is not a tutorial on protocol buffers itself; rather, it
is assumed that the reader has some knowledge on [protocol
buffers](https://developers.google.com/protocol-buffers/).

Setting up the repository configuration
---------------------------------------

Before we begin, we first need to declare where the root of our
workspace is located by creating the empty file `ROOT`:

``` sh
$ touch ROOT
```

The `protobuf` repository conveniently contains an
[example](https://github.com/protocolbuffers/protobuf/tree/v3.12.4/examples),
so we can use this and just add our own target files. We create file
`repos.template.json` as follows.

``` {.jsonc srcname="repos.template.json"}
{ "repositories":
  { "":
    { "repository":
      { "type": "zip"
      , "content": "7af7165b585e4aed714555a747b6822376176ef4"
      , "fetch": "https://github.com/protocolbuffers/protobuf/archive/refs/tags/v3.12.4.zip"
      , "subdir": "protobuf-3.12.4/examples"
      }
    , "target_root": "tutorial"
    , "bindings": {"rules": "rules-cc"}
    }
  , "tutorial": {"repository": {"type": "file", "path": "."}}
  }
}
```

The missing entry `"rules-cc"` refers to our C/C++ build rules provided
[online](https://github.com/just-buildsystem/rules-cc). These rules
support protobuf if the dependency `"protoc"` is provided. To import
this rule repository including the required transitive dependencies for
protobuf, the `just-import-git` script of the *justbuild* project can be
used with option `--as rules-cc` to generate the actual `repos.json`:

``` sh
$ just-import-git -C repos.template.json -b master --as rules-cc https://github.com/just-buildsystem/rules-cc > repos.json
```

To build the example with `just`, the only task is to write targets
files. As that contains a couple of new concepts, we will do this step
by step.

The proto library
-----------------

First, we have to declare the proto library. In this case, it only
contains the file `addressbook.proto` and has no dependencies. To
declare the library, create a `TARGETS` file with the following content:

``` {.jsonc srcname="TARGETS"}
{ "address":
  { "type": ["@", "rules", "proto", "library"]
  , "name": ["addressbook"]
  , "srcs": ["addressbook.proto"]
  }
}
```

In general, proto libraries could also depend on other proto libraries;
those would be added to the `"deps"` field.

When building the library, there's very little to do after `just-mr` fetches
and sets up all the repositories.

``` sh
$ just-mr build address
INFO: Performing repositories setup
INFO: Found 23 repositories to set up
PROG: [  0%] 1 local, 0 cached, 0 done
PROG: [  0%] 1 local, 0 cached, 0 done
PROG: [ 66%] 1 local, 1 cached, 14 done
PROG: [ 90%] 1 local, 1 cached, 19 done
PROG: [ 95%] 1 local, 1 cached, 20 done; 1 fetches ("rules-cc/just/import targets")
INFO: Setup finished, exec ["just","build","-C","...","--local-build-root","/tmp/proto","address"]
INFO: Requested target is [["@","","","address"],{}]
INFO: Analysed target [["@","","","address"],{}]
INFO: Discovered 0 actions, 0 trees, 0 blobs
INFO: Building [["@","","","address"],{}].
INFO: Processed 0 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
$
```

On the other hand, what did we expect? A proto library is an abstract
description of a protocol, so, as long as we don't specify for which
language we want to have bindings, there is nothing to generate.

Nevertheless, a proto library target is not empty. In fact, it can't be
empty, as other targets can only access the values of a target and have
no insights into its definitions. We already relied on this design
principle implicitly, when we exploited target-level caching for our
external dependencies and did not even construct the dependency graph
for that target. A proto library simply provides the dependency
structure of the `.proto` files.

``` sh
$ just-mr analyse --dump-nodes - address
INFO: Performing repositories setup
INFO: Found 23 repositories to set up
INFO: Setup finished, exec ["just","analyse","-C","...","--dump-nodes","-","address"]
INFO: Requested target is [["@","","","address"],{}]
INFO: Result of target [["@","","","address"],{}]: {
        "artifacts": {
        },
        "provides": {
          "proto": [
            {"id":"2a483a2de7f25c1bc066e47245f55ec9a2d4a719","type":"NODE"}
          ]
        },
        "runfiles": {
        }
      }
INFO: Target nodes of target [["@","","","address"],{}]:
{
  "089f6cae7ca77bb786578d3e0138b6ff445c5c92": {
    "result": {
      "artifact_stage": {
        "addressbook.proto": {
          "data": {
            "file_type": "f",
            "id": "b4b33b4c658924f0321ab4e7a9dc9cf8da1acec3",
            "size": 1234
          },
          "type": "KNOWN"
        }
      },
      "provides": {
      },
      "runfiles": {
      }
    },
    "type": "VALUE_NODE"
  },
  "2a483a2de7f25c1bc066e47245f55ec9a2d4a719": {
    "node_type": "library",
    "string_fields": {
      "name": ["addressbook"],
      "stage": [""]
    },
    "target_fields": {
      "deps": [],
      "srcs": [{"id":"089f6cae7ca77bb786578d3e0138b6ff445c5c92","type":"NODE"}]
    },
    "type": "ABSTRACT_NODE"
  }
}
$
```

The target has one provider `"proto"`, which is a node. Nodes are an
abstract representation of a target graph. More precisely, there are two
kind of nodes, and our example contains one of each.

The simple kind of nodes are the value nodes; they represent a target
that has a fixed value, and hence are given by artifacts, runfiles, and
provided data. In our case, we have one value node, the one for the
`.proto` file.

The other kind of nodes are the abstract nodes. They describe the
arguments for a target, but only have an abstract name (i.e., a string)
for the rule. Combining such an abstract target with a binding for the
abstract rule names gives a concrete "anonymous" target that, in our
case, will generate the library with the bindings for the concrete
language. In this example, the abstract name is `"library"`. The
alternative in our proto rules would have been `"service library"`, for
proto libraries that also contain `rpc` definitions (which is used by
[gRPC](https://grpc.io/)).

Using proto libraries
---------------------

Using proto libraries requires, as discussed, bindings for the abstract
names. Fortunately, our `CC` rules are aware of proto libraries, so we
can simply use them. Our target file hence continues as follows.

``` {.jsonc srcname="TARGETS"}
...
, "add_person":
  { "type": ["@", "rules", "CC", "binary"]
  , "name": ["add_person"]
  , "srcs": ["add_person.cc"]
  , "private-proto": ["address"]
  }
, "list_people":
  { "type": ["@", "rules", "CC", "binary"]
  , "name": ["list_people"]
  , "srcs": ["list_people.cc"]
  , "private-proto": ["address"]
  }
...
```

The first time, we build a target that requires the proto compiler (in
that particular version, built in that particular way), it takes a bit
of time, as the proto compiler has to be built. But in follow-up builds,
also in different projects, the target-level cache is filled already.

``` sh
$ just-mr build add_person
[...]
$ just-mr build add_person
INFO: Performing repositories setup
INFO: Found 23 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","add_person"]
INFO: Requested target is [["@","","","add_person"],{}]
INFO: Analysed target [["@","","","add_person"],{}]
INFO: Export targets found: 2 cached, 0 uncached, 0 not eligible for caching
INFO: Discovered 5 actions, 2 trees, 0 blobs
INFO: Building [["@","","","add_person"],{}].
INFO: Processed 5 actions, 5 cache hits.
INFO: Artifacts built, logical paths are:
        add_person [bca89ed8465e81c629d689b66c71deca138e2c27:2847912:x]
$
```

If we look at the actions associated with the binary, we find that those
are still the two actions we expect: a compile action and a link action.
(Some of the fields have been removed in the following example outputs and
replaced by `"..."` for clarity.)

``` sh
$ just-mr analyse add_person --dump-actions -
INFO: Performing repositories setup
INFO: Found 23 repositories to set up
INFO: Setup finished, exec ["just","analyse","-C","...","add_person","--dump-actions","-"]
INFO: Requested target is [["@","","","add_person"],{}]
INFO: Result of target [["@","","","add_person"],{}]: {
        "artifacts": {
          "add_person": {"data":{"id":"6ba3552427c6c47b52bc2fa571a64af300e27243","path":"add_person"},"type":"ACTION"}
        },
        "provides": {
          "package": {
            "to_bin": true
          },
          "run-libs": {
          }
        },
        "runfiles": {
        }
      }
INFO: Actions for target [["@","","","add_person"],{}]:
[
  {
    "command": ["c++","-O2",...,"-I","work","-isystem","include","-c","work/add_person.cc","-o","work/add_person.o"],
    "env": {
      "PATH": "/bin:/sbin:/usr/bin:/usr/sbin"
    },
    "input": {
      ...
    },
    "output": ["work/add_person.o"]
  },
  {
    "command": ["c++","-Wl,-rpath,$ORIGIN","-Wl,-rpath,$ORIGIN/../lib","-o","add_person","-O2",...,"add_person.o","libaddressbook.a","libprotobuf.a","libprotobuf-lite.a",...,"libz.a"],
    "env": {
      "PATH": "/bin:/sbin:/usr/bin:/usr/sbin"
    },
    "input": {
      ...
    },
    "output": ["add_person"]
  }
]
$
```

As discussed, the `libaddressbook.a` that is conveniently available
during the linking of the binary (as well as the `addressbook.pb.h`
available in the `include` tree for the compile action) are generated by
an anonymous target. Using that during the build we already filled the
target-level cache, we can have a look at all targets still analysed. In
the one anonymous target, we find again the abstract node we discussed
earlier.

``` sh
$ just-mr analyse add_person --dump-targets -
INFO: Performing repositories setup
INFO: Found 23 repositories to set up
INFO: Setup finished, exec ["just","analyse","-C","...","add_person","--dump-targets","-"]
INFO: Requested target is [["@","","","add_person"],{}]
INFO: Result of target [["@","","","add_person"],{}]: {
        "artifacts": {
          "add_person": {"data":{"id":"6ba3552427c6c47b52bc2fa571a64af300e27243","path":"add_person"},"type":"ACTION"}
        },
        "provides": {
          "package": {
            "to_bin": true
          },
          "run-libs": {
          }
        },
        "runfiles": {
        }
      }
INFO: List of analysed targets:
{
  "#": {
    "eda46ea21de25033ff7250e6a4cdc0b2c24be0c7": {
      "2a483a2de7f25c1bc066e47245f55ec9a2d4a719": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
    }
  },
  "@": {
    "": {
      "": {
        "add_person": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"ADD_LDFLAGS":null,"AR":null,"ARCH":null,"BUILD_POSITION_INDEPENDENT":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"LDFLAGS":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "address": [{}]
      }
    },
    "rules-cc": {
      "CC": {
        "defaults": [{"ARCH":null,"DEBUG":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
      },
      "CC/proto": {
        "defaults": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
      }
    },
    "rules-cc/just/protobuf": {
      "": {
        "installed protoc": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "libprotobuf": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "protoc": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "toolchain": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "toolchain_headers": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
      }
    },
    "rules-cc/just/rules": {
      "CC": {
        "defaults": [{"ARCH":null,"DEBUG":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
      }
    },
    "rules-cc/just/toolchain": {
      "CC": {
        "defaults": [{"ARCH":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "unknown": [{"ARCH":null,"HOST_ARCH":null,"TARGET_ARCH":null}]
      }
    }
  }
}
$
```

It should be noted, however, that this tight integration of proto into
our `C++` rules is just convenience of our code base. If we had to
cooperate with rules not aware of proto, we could have created a
separate rule delegating the library creation to the anonymous target
and then simply reflecting the values of that target. In fact, we could
simply use an empty library with a public `proto` dependency for this
purpose.

``` {.jsonc srcname="TARGETS"}
...
, "address proto library":
  {"type": ["@", "rules", "CC", "library"], "proto": ["address"]}
...
```

``` sh
$ just-mr analyse 'address proto library'
INFO: Performing repositories setup
INFO: Found 23 repositories to set up
INFO: Setup finished, exec ["just","analyse","-C","...","address proto library"]
INFO: Requested target is [["@","","","address proto library"],{}]
INFO: Result of target [["@","","","address proto library"],{}]: {
        "artifacts": {
        },
        "provides": {
          ...
          "compile-deps": {
            ...
            "addressbook.pb.h": {"data":{"id":"b05ff2058068a30961c3e28318b9105795a08a42","path":"work/addressbook.pb.h"},"type":"ACTION"},
            ...
          },
          "link-args": [
            "libaddressbook.a",
            ...
          ],
          "link-deps": {
            ...
            "libaddressbook.a": {"data":{"id":"acc15c9c1218b4df277f49d537a3c4b961263490","path":"libaddressbook.a"},"type":"ACTION"},
            ...
          },
          ...
        },
        "runfiles": {
        }
      }
$
```

Adding a test
-------------

Finally, let's add a test. As we use the `protobuf` repository as
workspace root, we add the test script ad hoc into a targets file, using
the `"file_gen"` rule. For debugging a potentially failing test, we also
keep the intermediate files the test generates. Add to the top-level
`TARGETS` file the following content:

``` {.jsonc srcname="TARGETS"}
...
, "test.sh":
  { "type": "file_gen"
  , "name": "test.sh"
  , "data":
    { "type": "join"
    , "separator": "\n"
    , "$1":
      [ "set -e"
      , "(echo 12345; echo 'John Doe'; echo 'jdoe@example.org'; echo) | ./add_person addressbook.data"
      , "./list_people addressbook.data > out.txt"
      , "grep Doe out.txt"
      ]
    }
  }
, "test":
  { "type": ["@", "rules", "shell/test", "script"]
  , "name": ["read-write-test"]
  , "test": ["test.sh"]
  , "deps": ["add_person", "list_people"]
  , "keep": ["addressbook.data", "out.txt"]
  }
...
```

That example also shows why it is important that the generation of the
language bindings is delegated to an anonymous target: we want to
analyse only once how the `C++` bindings are generated. Nevertheless,
many targets can depend (directly or indirectly) on the same proto
library. And, indeed, analysing the test, we get the expected additional
targets and the one anonymous target is reused by both binaries.

``` sh
$ just-mr analyse test --dump-targets -
INFO: Performing repositories setup
INFO: Found 23 repositories to set up
INFO: Setup finished, exec ["just","analyse","-C","...","test","--dump-targets","-"]
INFO: Requested target is [["@","","","test"],{}]
INFO: Result of target [["@","","","test"],{}]: {
        "artifacts": {
          "result": {"data":{"id":"e37a69e5d5213f53da5f1c2a42297c987732984f","path":"result"},"type":"ACTION"},
          "stderr": {"data":{"id":"e37a69e5d5213f53da5f1c2a42297c987732984f","path":"stderr"},"type":"ACTION"},
          "stdout": {"data":{"id":"e37a69e5d5213f53da5f1c2a42297c987732984f","path":"stdout"},"type":"ACTION"},
          "time-start": {"data":{"id":"e37a69e5d5213f53da5f1c2a42297c987732984f","path":"time-start"},"type":"ACTION"},
          "time-stop": {"data":{"id":"e37a69e5d5213f53da5f1c2a42297c987732984f","path":"time-stop"},"type":"ACTION"},
          "work/addressbook.data": {"data":{"id":"e37a69e5d5213f53da5f1c2a42297c987732984f","path":"work/addressbook.data"},"type":"ACTION"},
          "work/out.txt": {"data":{"id":"e37a69e5d5213f53da5f1c2a42297c987732984f","path":"work/out.txt"},"type":"ACTION"}
        },
        "provides": {
        },
        "runfiles": {
          "read-write-test": {"data":{"id":"71d9414c93b1bf4c2883af6070671761f9bc5cbf"},"type":"TREE"}
        }
      }
INFO: List of analysed targets:
{
  "#": {
    "eda46ea21de25033ff7250e6a4cdc0b2c24be0c7": {
      "2a483a2de7f25c1bc066e47245f55ec9a2d4a719": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
    }
  },
  "@": {
    "": {
      "": {
        "add_person": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"ADD_LDFLAGS":null,"AR":null,"ARCH":null,"BUILD_POSITION_INDEPENDENT":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"LDFLAGS":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "address": [{}],
        "list_people": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"ADD_LDFLAGS":null,"AR":null,"ARCH":null,"BUILD_POSITION_INDEPENDENT":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"LDFLAGS":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "test": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"ADD_LDFLAGS":null,"AR":null,"ARCH":null,"ARCH_DISPATCH":null,"BUILD_POSITION_INDEPENDENT":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"LDFLAGS":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"RUNS_PER_TEST":null,"TARGET_ARCH":null,"TEST_ENV":null,"TIMEOUT_SCALE":null,"TOOLCHAIN_CONFIG":null}],
        "test.sh": [{}]
      }
    },
    "rules-cc": {
      "CC": {
        "defaults": [{"ARCH":null,"DEBUG":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
      },
      "CC/proto": {
        "defaults": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
      }
    },
    "rules-cc/just/protobuf": {
      "": {
        "installed protoc": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "libprotobuf": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "protoc": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "toolchain": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "toolchain_headers": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
      }
    },
    "rules-cc/just/rules": {
      "CC": {
        "defaults": [{"ARCH":null,"DEBUG":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
      }
    },
    "rules-cc/just/toolchain": {
      "CC": {
        "defaults": [{"ARCH":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "unknown": [{"ARCH":null,"HOST_ARCH":null,"TARGET_ARCH":null}]
      }
    }
  }
}
INFO: Target tainted ["test"].
$
```

Finally, the test passes and the output is as expected.

``` sh
$ just-mr build test -P work/out.txt
INFO: Performing repositories setup
INFO: Found 23 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","test","-P","work/out.txt"]
INFO: Requested target is [["@","","","test"],{}]
INFO: Analysed target [["@","","","test"],{}]
INFO: Export targets found: 2 cached, 0 uncached, 0 not eligible for caching
INFO: Target tainted ["test"].
INFO: Discovered 8 actions, 5 trees, 1 blobs
INFO: Building [["@","","","test"],{}].
INFO: Processed 8 actions, 5 cache hits.
INFO: Artifacts built, logical paths are:
        result [7ef22e9a431ad0272713b71fdc8794016c8ef12f:5:f]
        stderr [e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:0:f]
        stdout [7fab9dd1ee66a1e76a3697a27524f905600afbd0:196:f]
        time-start [8e3614748ef049ea08bc49740d9ba55dfd42ef06:11:f]
        time-stop [8e3614748ef049ea08bc49740d9ba55dfd42ef06:11:f]
        work/addressbook.data [b673b647fbce717116104e84c335fdecaae974d6:41:f]
        work/out.txt [ae02d08605d72d74bbfa72d33e4d314bb3632591:101:f]
      (1 runfiles omitted.)
Person ID: 12345
  Name: John Doe
  E-mail address: jdoe@example.org
  Updated: 2024-04-19T15:51:27Z
INFO: Target tainted ["test"].
$
```
