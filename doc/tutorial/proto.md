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
protobuf, the `bin/just-import-git` script with option `--as rules-cc`
can be used to generate the actual `repos.json`:

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

When building the library, there's very little to do.

``` sh
$ just-mr build address
INFO: Requested target is [["@","","","address"],{}]
INFO: Analysed target [["@","","","address"],{}]
INFO: Export targets found: 0 cached, 0 uncached, 0 not eligible for caching
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
...
$ just-mr build add_person
INFO: Requested target is [["@","","","add_person"],{}]
INFO: Analysed target [["@","","","add_person"],{}]
INFO: Export targets found: 3 cached, 0 uncached, 0 not eligible for caching
INFO: Discovered 5 actions, 2 trees, 0 blobs
INFO: Building [["@","","","add_person"],{}].
INFO: Processed 5 actions, 5 cache hits.
INFO: Artifacts built, logical paths are:
        add_person [bcbb3deabfe0d77e6d3ea35615336a2f59a1b0aa:2285928:x]
$
```

If we look at the actions associated with the binary, we find that those
are still the two actions we expect: a compile action and a link action.

``` sh
$ just-mr analyse add_person --dump-actions -
INFO: Requested target is [["@","","","add_person"],{}]
INFO: Result of target [["@","","","add_person"],{}]: {
        "artifacts": {
          "add_person": {"data":{"id":"fcf211e2291b2867375e915538ce04cb4dfae86d","path":"add_person"},"type":"ACTION"}
        },
        "provides": {
        },
        "runfiles": {
        }
      }
INFO: Actions for target [["@","","","add_person"],{}]:
[
  {
    "command": ["c++","-I","work","-isystem","include","-c","work/add_person.cc","-o","work/add_person.o"],
    "env": {
      "PATH": "/bin:/usr/bin"
    },
    "input": {
      ...
    },
    "output": ["work/add_person.o"]
  },
  {
    "command": ["c++","-o","add_person","add_person.o","libaddressbook.a","libprotobuf.a","libprotobuf_lite.a","libzlib.a"],
    "env": {
      "PATH": "/bin:/usr/bin"
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
INFO: Requested target is [["@","","","add_person"],{}]
INFO: Result of target [["@","","","add_person"],{}]: {
        "artifacts": {
          "add_person": {"data":{"id":"fcf211e2291b2867375e915538ce04cb4dfae86d","path":"add_person"},"type":"ACTION"}
        },
        "provides": {
        },
        "runfiles": {
        }
      }
INFO: List of analysed targets:
{
  "#": {
    "eda46ea21de25033ff7250e6a4cdc0b2c24be0c7": {
      "2a483a2de7f25c1bc066e47245f55ec9a2d4a719": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
    }
  },
  "@": {
    "": {
      "": {
        "add_person": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "address": [{}]
      }
    },
    "rules-cc": {
      "CC": {
        "defaults": [{}]
      }
    },
    "rules-cc/just/protobuf": {
      "": {
        "C++ runtime": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "protoc": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "well_known_protos": [{}]
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
...
INFO: Requested target is [["@","","","address proto library"],{}]
INFO: Result of target [["@","","","address proto library"],{}]: {
        "artifacts": {
        },
        "provides": {
          ...
          "compile-deps": {
            "addressbook.pb.h": {"data":{"id":"6d70cd10fabcbc7591cd82aae2f100cca39d3879","path":"work/addressbook.pb.h"},"type":"ACTION"},
            ...
          },
          "link-args": [
            "libaddressbook.a",
            ...
          ],
          "link-deps": {
            "libaddressbook.a": {"data":{"id":"753073bd026b6470138c47e004469dd1d3df08d4","path":"libaddressbook.a"},"type":"ACTION"},
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
keep the intermediate files the test generates. Create a top-level
`TARGETS` file with the following content:

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
INFO: Requested target is [["@","","","test"],{}]
INFO: Result of target [["@","","","test"],{}]: {
        "artifacts": {
          "result": {"data":{"id":"20967787c42a289f5598249e696f851dde50065c","path":"result"},"type":"ACTION"},
          "stderr": {"data":{"id":"20967787c42a289f5598249e696f851dde50065c","path":"stderr"},"type":"ACTION"},
          "stdout": {"data":{"id":"20967787c42a289f5598249e696f851dde50065c","path":"stdout"},"type":"ACTION"},
          "time-start": {"data":{"id":"20967787c42a289f5598249e696f851dde50065c","path":"time-start"},"type":"ACTION"},
          "time-stop": {"data":{"id":"20967787c42a289f5598249e696f851dde50065c","path":"time-stop"},"type":"ACTION"},
          "work/addressbook.data": {"data":{"id":"20967787c42a289f5598249e696f851dde50065c","path":"work/addressbook.data"},"type":"ACTION"},
          "work/out.txt": {"data":{"id":"20967787c42a289f5598249e696f851dde50065c","path":"work/out.txt"},"type":"ACTION"}
        },
        "provides": {
        },
        "runfiles": {
          "read-write-test": {"data":{"id":"c9d7bfc5bc8448bfef25b4e73e4494560bf6c350"},"type":"TREE"}
        }
      }
INFO: List of analysed targets:
{
  "#": {
    "eda46ea21de25033ff7250e6a4cdc0b2c24be0c7": {
      "2a483a2de7f25c1bc066e47245f55ec9a2d4a719": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}]
    }
  },
  "@": {
    "": {
      "": {
        "add_person": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "address": [{}],
        "list_people": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "test": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"RUNS_PER_TEST":null,"TEST_ENV":null,"TOOLCHAIN_CONFIG":null}],
        "test.sh": [{}]
      }
    },
    "rules-cc": {
      "CC": {
        "defaults": [{}]
      }
    },
    "rules-cc/just/protobuf": {
      "": {
        "C++ runtime": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "protoc": [{"ADD_CFLAGS":null,"ADD_CXXFLAGS":null,"AR":null,"ARCH":null,"CC":null,"CFLAGS":null,"CXX":null,"CXXFLAGS":null,"DEBUG":null,"ENV":null,"HOST_ARCH":null,"OS":null,"TARGET_ARCH":null,"TOOLCHAIN_CONFIG":null}],
        "well_known_protos": [{}]
      }
    }
  }
}
INFO: Target tainted ["test"].
$
```

Finally, the test passes and the output is as expected.

``` sh
$ just-mr build test -Pwork/out.txt
INFO: Requested target is [["@","","","test"],{}]
INFO: Analysed target [["@","","","test"],{}]
INFO: Export targets found: 3 cached, 0 uncached, 0 not eligible for caching
INFO: Target tainted ["test"].
INFO: Discovered 8 actions, 4 trees, 1 blobs
INFO: Building [["@","","","test"],{}].
INFO: Processed 8 actions, 5 cache hits.
INFO: Artifacts built, logical paths are:
        result [7ef22e9a431ad0272713b71fdc8794016c8ef12f:5:f]
        stderr [e69de29bb2d1d6434b8b29ae775ad8c2e48c5391:0:f]
        stdout [7fab9dd1ee66a1e76a3697a27524f905600afbd0:196:f]
        time-start [7ac216a2a98b7739ae5304d96cdfa6f0b0ed87b6:11:f]
        time-stop [7ac216a2a98b7739ae5304d96cdfa6f0b0ed87b6:11:f]
        work/addressbook.data [baa6f28731ff6d93fbef9fcc5f7e8ae900da5ba5:41:f]
        work/out.txt [7fb178dd66ecf24fdb786a0f96ae5969b55442da:101:f]
      (1 runfiles omitted.)
Person ID: 12345
  Name: John Doe
  E-mail address: jdoe@example.org
  Updated: 2022-12-14T18:08:36Z
INFO: Target tainted ["test"].
$
```
