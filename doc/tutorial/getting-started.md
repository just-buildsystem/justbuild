Getting Started
===============

In order to use *justbuild*, first make sure that `just`, `just-mr`, and
`just-import-git` are available in your `PATH`.

Creating a new project
----------------------

*justbuild* needs to know the root of the project worked on. By default,
it searches upwards from the current directory till it finds a marker.
Currently, we support three different markers: the files `ROOT` and
`WORKSPACE` or the directory `.git`. Lets create a new project by
creating one of those markers:

``` sh
$ touch ROOT
```

Creating a generic target
-------------------------

By default, targets are described in `TARGETS` files. These files
contain a `JSON` object with the target name as key and the target
description as value. A target description is an object with at least a
single mandatory field: `"type"`. This field specifies which rule
(built-in or user-defined) to apply for this target.

A simple target that only executes commands can be created using the
built-in `"generic"` rule, which requires at least one command and one
output file or directory. To create such a target, create the file
`TARGETS` with the following content:

``` {.jsonc srcname="TARGETS"}
{ "greeter":
  { "type": "generic"
  , "cmds": ["echo -n 'Hello ' > out.txt", "cat name.txt >> out.txt"]
  , "outs": ["out.txt"]
  , "deps": ["name.txt"]
  }
}
```

In this example, the `"greeter"` target will run two commands to produce
the output file `out.txt`. The second command depends on the input file
`name.txt` that we need to create as well:

``` sh
$ echo World > name.txt
```

Building a generic target
-------------------------

To build a target, we need to run `just` with the subcommand `build`:

``` sh
$ just build greeter
INFO: Requested target is [["@","","","greeter"],{}]
INFO: Analysed target [["@","","","greeter"],{}]
INFO: Discovered 1 actions, 0 trees, 0 blobs
INFO: Building [["@","","","greeter"],{}].
INFO: Processed 1 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        out.txt [557db03de997c86a4a028e1ebd3a1ceb225be238:12:f]
$
```

The subcommand `build` just builds the artifact but does not stage it to
any user-defined location on the file system. Instead it reports a
description of the artifact consisting of `git` blob identifier, size,
and type (in this case `f` for non-executable file). To also stage the
produced artifact to the working directory, use the `install` subcommand
and specify the output directory:

``` sh
$ just install greeter -o .
INFO: Requested target is [["@","","","greeter"],{}]
INFO: Analysed target [["@","","","greeter"],{}]
INFO: Discovered 1 actions, 0 trees, 0 blobs
INFO: Building [["@","","","greeter"],{}].
INFO: Processed 1 actions, 1 cache hits.
INFO: Artifacts can be found in:
        /tmp/tutorial/out.txt [557db03de997c86a4a028e1ebd3a1ceb225be238:12:f]
$ cat out.txt
Hello World
$
```

Note that the `install` subcommand initiates the build a second time,
without executing any actions as all actions are being served from
cache. The produced artifact is identical, which is indicated by the
same hash/size/type.

If one is only interested in one of the final artifacts, one can also
request via the `-P` option that this particular artifact be written
to standard output after the build; if the target produces only a
single artifact, the flag `-p` can be used instead as well (without
the need of specifying the artifact). As all messages are reported
to standard error, this can be used for both, interactively reading
a text file, as well as for piping the artifact to another program.

``` sh
$ just build greeter -P out.txt
INFO: Requested target is [["@","","","greeter"],{}]
INFO: Analysed target [["@","","","greeter"],{}]
INFO: Discovered 1 actions, 0 trees, 0 blobs
INFO: Building [["@","","","greeter"],{}].
INFO: Processed 1 actions, 1 cache hits.
INFO: Artifacts built, logical paths are:
        out.txt [557db03de997c86a4a028e1ebd3a1ceb225be238:12:f]
Hello World
$ just build -p
INFO: Requested target is [["@","","","greeter"],{}]
INFO: Analysed target [["@","","","greeter"],{}]
INFO: Discovered 1 actions, 0 trees, 0 blobs
INFO: Building [["@","","","greeter"],{}].
INFO: Processed 1 actions, 1 cache hits.
INFO: Artifacts built, logical paths are:
        out.txt [557db03de997c86a4a028e1ebd3a1ceb225be238:12:f]
Hello World
$
```

In the last example we used that, if no target is specified, the
lexicographically first one is taken.

Alternatively, we could also directly request the artifact `out.txt`
from *justbuild*'s CAS (content-addressable storage) and print it on
the command line via:

``` sh
$ just install-cas [557db03de997c86a4a028e1ebd3a1ceb225be238:12:f]
Hello World
$
```

The canonical way of requesting an object from the CAS is, as just
shown, to specify the full triple of hash, size, and type, separated by
colons and enclosed in square brackets. To simplify usage, the brackets
can be omitted and the size and type fields have the default values `0`
and `f`, respectively. While the default value for the size is wrong for
all but one string, the hash still determines the content of the file
and hence the local CAS is still able to retrieve the file. So the
typical invocation would simply specify the hash.

``` sh
$ just install-cas 557db03de997c86a4a028e1ebd3a1ceb225be238
Hello World
$
```

Targets versus Files: The Stage
-------------------------------

When invoking the `build` command, we had to specify the target
`greeter`, not the output file `out.txt`. While other build systems
allow requests specifying an output file, for *justbuild* this would
conflict with a fundamental design principle: staging; each target has
its own logical output space, the "stage", where it can put its
artifacts. We can, without any problem, add a second target also
generating a file `out.txt`.

``` {.jsonc srcname="TARGETS"}
...
, "upper":
  { "type": "generic"
  , "cmds": ["cat name.txt | tr a-z A-Z > out.txt"]
  , "outs": ["out.txt"]
  , "deps": ["name.txt"]
  }
...
```

As we only request targets, no conflicts arise.

``` sh
$ just build upper -p
INFO: Requested target is [["@","","","upper"],{}]
INFO: Analysed target [["@","","","upper"],{}]
INFO: Discovered 1 actions, 0 trees, 0 blobs
INFO: Building [["@","","","upper"],{}].
INFO: Processed 1 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        out.txt [83cf24cdfb4891a36bee93421930dd220766299a:6:f]
WORLD
$ just build greeter -p
INFO: Requested target is [["@","","","greeter"],{}]
INFO: Analysed target [["@","","","greeter"],{}]
INFO: Discovered 1 actions, 0 trees, 0 blobs
INFO: Building [["@","","","greeter"],{}].
INFO: Processed 1 actions, 1 cache hits.
INFO: Artifacts built, logical paths are:
        out.txt [557db03de997c86a4a028e1ebd3a1ceb225be238:12:f]
Hello World
$
```

While one normally tries to design targets in such a way that they
don't have conflicting files if they should be used together, it is up
to the receiving target to decide what to do with those artifacts. A
built-in rule allowing to rearrange artifacts is `"install"`; a detailed
description of this rule can be found in the documentation. In the
simple case of a target producing precisely one file, the argument
`"files"` can be used to remap that file. Note that the mapping is
from the desired location to the target name (representing the single
artifact of that target) as such a mapping is necessarily conflict free.

``` {.jsonc srcname="TARGETS"}
...
, "both":
  {"type": "install", "files": {"hello.txt": "greeter", "upper.txt": "upper"}}
...
```

``` sh
$ just build both
INFO: Requested target is [["@","","","both"],{}]
INFO: Analysed target [["@","","","both"],{}]
INFO: Discovered 2 actions, 0 trees, 0 blobs
INFO: Building [["@","","","both"],{}].
INFO: Processed 2 actions, 2 cache hits.
INFO: Artifacts built, logical paths are:
        hello.txt [557db03de997c86a4a028e1ebd3a1ceb225be238:12:f]
        upper.txt [83cf24cdfb4891a36bee93421930dd220766299a:6:f]
$
```

Inspecting internals: Analyse
-----------------------------

For our simple targets, it is easy to remember what they look like,
and what the build steps are. For more complex projects this might
no longer be the case. Therefore, *justbuild* tries to be very
transparent about its understanding of the build. The command
to do so is called `analyse` and also takes a target as argument.
It shows all(!) the information available to a target depending
on it. In the case of `greeter` this is just a single artifact.

``` sh
$ just analyse greeter
INFO: Requested target is [["@","","","greeter"],{}]
INFO: Analysed target [["@","","","greeter"],{}]
INFO: Result of target [["@","","","greeter"],{}]: {
        "artifacts": {
          "out.txt": {"data":{"id":"28c9f5feb17361a5f755b19961b4c80c651586269786d93e58bfff5b0038c620","path":"out.txt"},"type":"ACTION"}
        },
        "provides": {
        },
        "runfiles": {
        }
      }
INFO: Target tainted ["test"].
```

As we can see, the artifact will be installed at the logical
path `out.txt`. The artifact itself is generated by a build
action, more precisely, it is the output `out.txt` of that
action. Actions, in general, can have more than one output.

For the target `both` we find the same artifact at a different
output location in this case `hello.txt`.

``` sh
$ just analyse both
INFO: Requested target is [["@","","","both"],{}]
INFO: Analysed target [["@","","","both"],{}]
INFO: Result of target [["@","","","both"],{}]: {
        "artifacts": {
          "hello.txt": {"data":{"id":"28c9f5feb17361a5f755b19961b4c80c651586269786d93e58bfff5b0038c620","path":"out.txt"},"type":"ACTION"},
          "upper.txt": {"data":{"id":"f122c2af488a56c94d08650a9d799dbb910484409116b8e0c071e198082256ef","path":"out.txt"},"type":"ACTION"}
        },
        "provides": {
        },
        "runfiles": {
          "hello.txt": {"data":{"id":"28c9f5feb17361a5f755b19961b4c80c651586269786d93e58bfff5b0038c620","path":"out.txt"},"type":"ACTION"},
          "upper.txt": {"data":{"id":"f122c2af488a56c94d08650a9d799dbb910484409116b8e0c071e198082256ef","path":"out.txt"},"type":"ACTION"}
        }
      }
INFO: Target tainted ["test"].
```

We also see another technical detail. As an `install` target,
`both` also has the same artifact in the `runfiles` arrangement of
files. Having two dedicated arrangements of artifacts, that are both
installed when the target is installed, can be useful to distinguish
between the main artifact and files needed when building against
that target (e.g., to distinguish a library from its header files).
As a very generic built-in rule, however, `install` simply makes
all files available in both arrangements.

The canonical way to obtain all action definitions is to dump the
action graph using the `--dump-graph` option.

``` sh
$ just analyse both --dump-graph all-actions.json
$ cat all-actions.json
```

As we know that the action was generated by the `greeter` target,
was can also ask that target for the actions associated with it
using the `--dump-actions` option. The `install` target does not
generate any actions.

``` sh
$ just analyse greeter --dump-actions -
INFO: Requested target is [["@","","","greeter"],{}]
INFO: Analysed target [["@","","","greeter"],{}]
INFO: Result of target [["@","","","greeter"],{}]: {
        "artifacts": {
          "out.txt": {"data":{"id":"28c9f5feb17361a5f755b19961b4c80c651586269786d93e58bfff5b0038c620","path":"out.txt"},"type":"ACTION"}
        },
        "provides": {
        },
        "runfiles": {
        }
      }
INFO: Actions for target [["@","","","greeter"],{}]:
[
  {
    "command": ["sh","-c","echo -n 'Hello ' > out.txt\ncat name.txt >> out.txt\n"],
    "input": {
      "name.txt": {
        "data": {
          "path": "name.txt",
          "repository": ""
        },
        "type": "LOCAL"
      }
    },
    "output": ["out.txt"]
  }
]
$ just analyse both --dump-actions -
INFO: Requested target is [["@","","","both"],{}]
INFO: Analysed target [["@","","","both"],{}]
INFO: Result of target [["@","","","both"],{}]: {
        "artifacts": {
          "hello.txt": {"data":{"id":"28c9f5feb17361a5f755b19961b4c80c651586269786d93e58bfff5b0038c620","path":"out.txt"},"type":"ACTION"},
          "upper.txt": {"data":{"id":"f122c2af488a56c94d08650a9d799dbb910484409116b8e0c071e198082256ef","path":"out.txt"},"type":"ACTION"}
        },
        "provides": {
        },
        "runfiles": {
          "hello.txt": {"data":{"id":"28c9f5feb17361a5f755b19961b4c80c651586269786d93e58bfff5b0038c620","path":"out.txt"},"type":"ACTION"},
          "upper.txt": {"data":{"id":"f122c2af488a56c94d08650a9d799dbb910484409116b8e0c071e198082256ef","path":"out.txt"},"type":"ACTION"}
        }
      }
INFO: Actions for target [["@","","","both"],{}]:
[
]
$
```

However, those actions are not owned by a target in any way.
Actions are identified by their definition in the same way as
`git` identifies a blob by its sequence of bytes and names it by
a suitable hash value. So, let's add another target defining an
action in the same way.

``` {.jsonc srcname="TARGETS"}
...
, "another greeter":
  { "type": "generic"
  , "cmds": ["echo -n 'Hello ' > out.txt", "cat name.txt >> out.txt"]
  , "outs": ["out.txt"]
  , "deps": ["name.txt"]
  }
...
```

Then we find that it defines the same artifact.

``` sh
$ just analyse 'another greeter'
INFO: Requested target is [["@","","","another greeter"],{}]
INFO: Analysed target [["@","","","another greeter"],{}]
INFO: Result of target [["@","","","another greeter"],{}]: {
        "artifacts": {
          "out.txt": {"data":{"id":"28c9f5feb17361a5f755b19961b4c80c651586269786d93e58bfff5b0038c620","path":"out.txt"},"type":"ACTION"}
        },
        "provides": {
        },
        "runfiles": {
        }
      }
```

This is also the notion of equality used to detect conflicts when
analysing targets before running any actions.
