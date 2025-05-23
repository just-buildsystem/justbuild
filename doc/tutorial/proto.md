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
INFO: Found 23 repositories involved
PROG: [ 44%] 0 computed, 1 local, 13 cached, 4 done; 7 fetches ("rules-cc/just/defaults", ...)
PROG: [ 66%] 0 computed, 1 local, 13 cached, 6 done; 7 fetches ("rules-cc/just/defaults", ...)
PROG: [ 66%] 0 computed, 1 local, 13 cached, 6 done; 7 fetches ("rules-cc/just/defaults", ...)
PROG: [ 77%] 0 computed, 1 local, 13 cached, 7 done; 7 fetches ("rules-cc/just/defaults", ...)
INFO: Setup finished, exec ["just","build","-C","...","--local-build-root","/tmp/proto","address"]
INFO: Requested target is [["@","","","address"],{}]
INFO: Analysed target [["@","","","address"],{}]
INFO: Discovered 0 actions, 0 tree overlays, 0 trees, 0 blobs
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
INFO: Found 23 repositories involved
INFO: Setup finished, exec ["just","analyse","-C","...","--dump-nodes","-","address"]
INFO: Requested target is [["@","","","address"],{}]
INFO: Result of target [["@","","","address"],{}]: {
        "artifacts": {
        },
        "provides": {
          "proto": [
            {"id":"6bcfb07e77f4d00f84d4c38bff64b92e0a1cf07399bd0987250eaef1b06b0b50","type":"NODE"}
          ]
        },
        "runfiles": {
        }
      }
INFO: Target nodes of target [["@","","","address"],{}]:
{
  "6bcfb07e77f4d00f84d4c38bff64b92e0a1cf07399bd0987250eaef1b06b0b50": {
    "node_type": "library",
    "string_fields": {
      "name": ["addressbook"],
      "stage": [""]
    },
    "target_fields": {
      "deps": [],
      "srcs": [{"id":"dd79fcd0043ad155b5765f6a7a58a6c88fbcd38567ab0523684bd54a925727ce","type":"NODE"}]
    },
    "type": "ABSTRACT_NODE"
  },
  "dd79fcd0043ad155b5765f6a7a58a6c88fbcd38567ab0523684bd54a925727ce": {
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
INFO: Found 23 repositories involved
INFO: Setup finished, exec ["just","build","-C","...","add_person"]
INFO: Requested target is [["@","","","add_person"],{}]
INFO: Analysed target [["@","","","add_person"],{}]
INFO: Export targets found: 2 cached, 0 uncached, 0 not eligible for caching
INFO: Discovered 5 actions, 0 tree overlays, 2 trees, 0 blobs
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
INFO: Found 23 repositories involved
INFO: Setup finished, exec ["just","analyse","-C","...","add_person","--dump-actions","-"]
INFO: Requested target is [["@","","","add_person"],{}]
INFO: Result of target [["@","","","add_person"],{}]: {
        "artifacts": {
          "add_person": {"data":{"id":"cb403cfeb7af26f83cb268056847f465d330ac44f7a563788305436b2640df2e","path":"work/add_person"},"type":"ACTION"}
        },
        "provides": {
          "debug-hdrs": {
          },
          "debug-srcs": {
          },
          "dwarf-pkg": {
          },
          "lint": [
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
INFO: Found 23 repositories involved
INFO: Setup finished, exec ["just","analyse","-C","...","add_person","--dump-targets","-"]
INFO: Requested target is [["@","","","add_person"],{}]
INFO: Result of target [["@","","","add_person"],{}]: {
        "artifacts": {
          "add_person": {"data":{"id":"cb403cfeb7af26f83cb268056847f465d330ac44f7a563788305436b2640df2e","path":"work/add_person"},"type":"ACTION"}
        },
        "provides": {
          "debug-hdrs": {
          },
          "debug-srcs": {
          },
          "dwarf-pkg": {
          },
          "lint": [
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
INFO: List of analysed targets:
{
  "#": {
    "c4f68b96f739e96f894c5b498ab2b3f0bc62df120419094f867b1d5769f5e4fa": {
      "6bcfb07e77f4d00f84d4c38bff64b92e0a1cf07399bd0987250eaef1b06b0b50": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
    }
  },
  "@": {
    "": {
      "": {
        "add_person": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"ADD_LDFLAGS":null,"AR":null,"ARCH":null,"BUILD_POSITION_INDEPENDENT":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"DWP":null,"ENV":null,"HOST_ARCH":null,"LDFLAGS":null,"LINT":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
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
        "defaults": [{"ARCH":null,"DEBUG":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "unknown": [{"ARCH":null,"DEBUG":null,"HOST_ARCH":null,"TARGET_ARCH":null}]
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
INFO: Found 23 repositories involved
INFO: Setup finished, exec ["just","analyse","-C","...","address proto library"]
INFO: Requested target is [["@","","","address proto library"],{}]
INFO: Result of target [["@","","","address proto library"],{}]: {
        "artifacts": {
        },
        "provides": {
          ...
          "compile-deps": {
            ...
            "addressbook.pb.h": {"data":{"id":"bab5472beed7f032000d2d2cbf7d772c22c8c95cccdecbe70e73e9494ee34bf0","path":"work/addressbook.pb.h"},"type":"ACTION"},
            ...
          },
          ...
          "link-args": [
            "libaddressbook.a",
            ...
          ],
          "link-deps": {
            ...
            "libaddressbook.a": {"data":{"id":"fa60ded058e1fc7663b4b925a1274d1fe330bedb12c8785a7042e450ef17a7c1","path":"work/libaddressbook.a"},"type":"ACTION"},
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

Now, let's add a test. As we use the `protobuf` repository as
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
INFO: Found 23 repositories involved
INFO: Setup finished, exec ["just","analyse","-C","...","test","--dump-targets","-"]
INFO: Requested target is [["@","","","test"],{}]
INFO: Result of target [["@","","","test"],{}]: {
        "artifacts": {
          "pwd": {"data":{"id":"73744cdcd8be5082a6960ec0cf8c929d2a9cd0dd65860e3458901c49c8e4744f","path":"pwd"},"type":"ACTION"},
          "result": {"data":{"id":"73744cdcd8be5082a6960ec0cf8c929d2a9cd0dd65860e3458901c49c8e4744f","path":"result"},"type":"ACTION"},
          "stderr": {"data":{"id":"73744cdcd8be5082a6960ec0cf8c929d2a9cd0dd65860e3458901c49c8e4744f","path":"stderr"},"type":"ACTION"},
          "stdout": {"data":{"id":"73744cdcd8be5082a6960ec0cf8c929d2a9cd0dd65860e3458901c49c8e4744f","path":"stdout"},"type":"ACTION"},
          "time-start": {"data":{"id":"73744cdcd8be5082a6960ec0cf8c929d2a9cd0dd65860e3458901c49c8e4744f","path":"time-start"},"type":"ACTION"},
          "time-stop": {"data":{"id":"73744cdcd8be5082a6960ec0cf8c929d2a9cd0dd65860e3458901c49c8e4744f","path":"time-stop"},"type":"ACTION"},
          "work/addressbook.data": {"data":{"id":"73744cdcd8be5082a6960ec0cf8c929d2a9cd0dd65860e3458901c49c8e4744f","path":"work/addressbook.data"},"type":"ACTION"},
          "work/out.txt": {"data":{"id":"73744cdcd8be5082a6960ec0cf8c929d2a9cd0dd65860e3458901c49c8e4744f","path":"work/out.txt"},"type":"ACTION"}
        },
        "provides": {
          "lint": [
          ]
        },
        "runfiles": {
          "read-write-test": {"data":{"id":"89bfa89b538549aa812415be625759872191d05315726828829848a87f86fa9f"},"type":"TREE"}
        }
      }
INFO: List of analysed targets:
{
  "#": {
    "c4f68b96f739e96f894c5b498ab2b3f0bc62df120419094f867b1d5769f5e4fa": {
      "6bcfb07e77f4d00f84d4c38bff64b92e0a1cf07399bd0987250eaef1b06b0b50": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
    }
  },
  "@": {
    "": {
      "": {
        "add_person": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"ADD_LDFLAGS":null,"AR":null,"ARCH":null,"BUILD_POSITION_INDEPENDENT":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"DWP":null,"ENV":null,"HOST_ARCH":null,"LDFLAGS":null,"LINT":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "address": [{}],
        "list_people": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"ADD_LDFLAGS":null,"AR":null,"ARCH":null,"BUILD_POSITION_INDEPENDENT":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"DWP":null,"ENV":null,"HOST_ARCH":null,"LDFLAGS":null,"LINT":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "test": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"ADD_LDFLAGS":null,"AR":null,"ARCH":null,"ARCH_DISPATCH":null,"BUILD_POSITION_INDEPENDENT":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"DWP":null,"ENV":null,"HOST_ARCH":null,"LDFLAGS":null,"LINT":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"RUNS_PER_TEST":null,"TARGET_ARCH":null,"TEST_ENV":null,"TEST_SUMMARY_EXECUTION_PROPERTIES":null,"TIMEOUT_SCALE":null,"TOOLCHAIN_CONFIG":null}],
        "test.sh": [{}]
      }
    },
    "rules-cc": {
      "CC": {
        "defaults": [{"ARCH":null,"DEBUG":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
      },
      "CC/proto": {
        "defaults": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"PKG_CONFIG_ARGS":null,"PREFIX":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
      },
      "shell": {
        "defaults": [{"ARCH":null,"HOST_ARCH":null,"TARGET_ARCH":null}]
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
        "defaults": [{"ARCH":null,"DEBUG":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "unknown": [{"ARCH":null,"DEBUG":null,"HOST_ARCH":null,"TARGET_ARCH":null}]
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
INFO: Found 23 repositories involved
INFO: Setup finished, exec ["just","build","-C","...","test","-P","work/out.txt"]
INFO: Requested target is [["@","","","test"],{}]
INFO: Analysed target [["@","","","test"],{}]
INFO: Export targets found: 2 cached, 0 uncached, 0 not eligible for caching
INFO: Target tainted ["test"].
INFO: Discovered 8 actions, 0 tree overlays, 5 trees, 1 blobs
INFO: Building [["@","","","test"],{}].
INFO: Processed 8 actions, 5 cache hits.
INFO: Artifacts built, logical paths are:
        pwd [9006e78b54c8f3118918d2d471c79745ffa0c8a0:311:f]
        result [7ef22e9a431ad0272713b71fdc8794016c8ef12f:5:f]
        stderr [e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:0:f]
        stdout [7fab9dd1ee66a1e76a3697a27524f905600afbd0:196:f]
        time-start [0836c60e080d14176345259e32f65c1e14edfc49:11:f]
        time-stop [0836c60e080d14176345259e32f65c1e14edfc49:11:f]
        work/addressbook.data [036614cb6ec77ecf979729cbef3f22bd5ebd6a56:41:f]
        work/out.txt [90ffe98823f628b92fe54b8c85f1754a9085c8db:101:f]
      (1 runfiles omitted.)
Person ID: 12345
  Name: John Doe
  E-mail address: jdoe@example.org
  Updated: ...
INFO: Target tainted ["test"].
$
```

Debugging with generated files
------------------------------

Finally, let's look at how debugging using protobufs looks like. In the root
`TARGETS` file we can add a debug version of the `"list_people"` binary,
together with the target to stage its respective debug artifacts (as described
in the chapter on [*Debugging*](./hello-world.md)).

``` {.jsonc srcname="TARGETS"}
...
, "list_people debug":
  { "type": "configure"
  , "target": "list_people"
  , "config": {"type": "'", "$1": {"DEBUG": {"USE_DEBUG_FISSION": false}}}
  }
, "list_people debug staged":
  { "type": ["@", "rules", "CC", "install-with-deps"]
  , "targets": ["list_people debug"]
  }
...
```

The debugging target can then be installed to a directory of our choosing.

``` sh
$ just-mr install "list_people debug staged" -o .ext/debug
INFO: Performing repositories setup
INFO: Found 23 repositories involved
INFO: Setup finished, exec ["just","install","-C","...","list_people debug staged","-o",".ext/debug"]
INFO: Requested target is [["@","","","list_people debug staged"],{}]
INFO: Analysed target [["@","","","list_people debug staged"],{}]
INFO: Export targets found: 0 cached, 98 uncached, 0 not eligible for caching
INFO: Discovered 447 actions, 0 tree overlays, 99 trees, 1 blobs
INFO: Building [["@","","","list_people debug staged"],{}].
[...]
INFO: Processed 447 actions, 1 cache hits.
INFO: Artifacts can be found in:
        /tmp/tutorial/.ext/debug/bin/list_people [71165d3aed4491169ed8bb8e783a9bbc15b9e9b6:46902576:x]
        /tmp/tutorial/.ext/debug/include/absl/algorithm/algorithm.h [59aeed7d264d927e44648428056cc9c489fad844:2190:f]
        /tmp/tutorial/.ext/debug/include/absl/algorithm/container.h [6bbe3b5adf40b8a4bc48551f8e9f0af96dd98ace:80031:f]
        ...
        /tmp/tutorial/.ext/debug/include/absl/utility/utility.h [ebbb49b7159a3bcf2f1edbf9d44bd67dceb0329f:7646:f]
        /tmp/tutorial/.ext/debug/include/addressbook.pb.h [f273099fe8d8345b1e61bdfa19751aefebe55869:46787:f]
        /tmp/tutorial/.ext/debug/include/google/protobuf/any.h [fe8a1775c77b91fc08368833d70236509dde82b7:6127:f]
        /tmp/tutorial/.ext/debug/include/google/protobuf/any.pb.h [67185ebca130f840ddacd687c59faceb523730e4:16787:f]
        /tmp/tutorial/.ext/debug/include/google/protobuf/any.proto [eff44e5099da27f7fb1ef14bb34902ccf4250b89:6154:f]
        ...
        /tmp/tutorial/.ext/debug/include/google/protobuf/wrappers.pb.h [0d85637aa1dadd86ccbc81a9563d54aba9eb9b3b:77623:f]
        /tmp/tutorial/.ext/debug/include/google/protobuf/wrappers.proto [1959fa55a4e7f284a9d6a78a447c5d89d137e87c:4044:f]
        /tmp/tutorial/.ext/debug/include/utf8_range.h [d7c232616022bb28c8bc35ca62d61f890a2f8ced:540:f]
        /tmp/tutorial/.ext/debug/include/utf8_validity.h [1f251d0fec0a2ae496335780d93a2869a1d7f743:712:f]
        /tmp/tutorial/.ext/debug/include/zconf.h [62adc8d8431f2f9149ae0b1583915e21a28dd8b5:16500:f]
        /tmp/tutorial/.ext/debug/include/zlib.h [8d4b932eaf6a0fbb8133b3ab49ba5ef587059fa0:96829:f]
        /tmp/tutorial/.ext/debug/work/absl/algorithm/algorithm.h [59aeed7d264d927e44648428056cc9c489fad844:2190:f]
        /tmp/tutorial/.ext/debug/work/absl/algorithm/container.h [6bbe3b5adf40b8a4bc48551f8e9f0af96dd98ace:80031:f]
        ...
        /tmp/tutorial/.ext/debug/work/absl/utility/utility.h [ebbb49b7159a3bcf2f1edbf9d44bd67dceb0329f:7646:f]
        /tmp/tutorial/.ext/debug/work/addressbook.pb.cc [2261b2349a90b090d501c3c3c362e1efc783c218:46574:f]
        /tmp/tutorial/.ext/debug/work/addressbook.pb.h [f273099fe8d8345b1e61bdfa19751aefebe55869:46787:f]
        ...
        /tmp/tutorial/.ext/debug/work/google/protobuf/any.h [fe8a1775c77b91fc08368833d70236509dde82b7:6127:f]
        /tmp/tutorial/.ext/debug/work/google/protobuf/any.pb.h [67185ebca130f840ddacd687c59faceb523730e4:16787:f]
        /tmp/tutorial/.ext/debug/work/google/protobuf/any.proto [eff44e5099da27f7fb1ef14bb34902ccf4250b89:6154:f]
        ...
        /tmp/tutorial/.ext/debug/work/list_people.cc [b309c596804739007510f06ff62c12898275fec7:2268:f]
        ...
        /tmp/tutorial/.ext/debug/work/zlib.h [8d4b932eaf6a0fbb8133b3ab49ba5ef587059fa0:96829:f]
        /tmp/tutorial/.ext/debug/work/zutil.c [b1c5d2d3c6daf5a4b7a337dafe3e862ca177b41c:7179:f]
        /tmp/tutorial/.ext/debug/work/zutil.h [48dd7febae65eeeaad7794f0a9317bcd054c107f:6677:f]
INFO: Backing up artifacts of 98 export targets
```

As the command requires (re)building many targets in debug mode, some of the
resulting output was replaced with `"..."` for brevity.
What needs to be noted here is that for each (directly or transitively)
included proto file the corresponding _generated_ `.pb.h` header file and
`.pb.cc` source file get staged as well, ensuring that also all needed proto
symbols will be available to a debugger.

In order to debug now this binary, one can, for example, use the
`addressbook.data` file generated by the previous test, staged accordingly, as
input database for the `list_people` binary.

``` sh
$ just-mr install test -o .ext/test_data
[...]
$ cd .ext/debug
$ gdb --args bin/list_people ../test_data/work/addressbook.data
```
