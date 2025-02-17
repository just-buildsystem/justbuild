Target versus `FILE`, `GLOB`, and `TREE`
========================================

So far, we referred to defined targets as well as source files by their
name and it just worked. When considering third-party software we
already saw the `TREE` reference. In this section, we will highlight in
more detail the ways to refer to sources, as well as the difference
between defined and source targets. The latter is used, e.g., when
third-party software has to be patched.

As example for this section we use
[gnu units](https://www.gnu.org/software/units/) where we want to patch into
the standard units definition add two units of area popular in German news.

Repository Config for `units` with patches
------------------------------------------

Before we begin, we first need to declare where the root of our
workspace is located by creating the empty file `ROOT`:

``` sh
$ touch ROOT
```

The sources are an archive available on the web. As upstream uses a
different build system, we have to provide our own build description; we
take the top-level directory as layer for this. As we also want to patch
the definition file, we add the subdirectory `files` as logical
repository for the patches. Hence we create a file `repos.json` with the
following content.

``` {.jsonc srcname="repos.json"}
{ "main": "units"
, "repositories":
  { "rules-cc":
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "3a5f0f0f50c59495ffc3b198df59e6edb8416450"
      , "repository": "https://github.com/just-buildsystem/rules-cc.git"
      , "subdir": "rules"
      }
    }
  , "import targets": {"repository": {"type": "file", "path": "."}}
  , "patches": {"repository": {"type": "file", "path": "files"}}
  , "units":
    { "repository":
      { "type": "archive"
      , "content": "9781174d42bd593d3bab6c6decfdcae60e3ce328"
      , "fetch": "https://ftp.gnu.org/gnu/units/units-2.21.tar.gz"
      , "subdir": "units-2.21"
      }
    , "target_root": "import targets"
    , "target_file_name": "TARGETS.units"
    , "bindings": {"rules": "rules-cc", "patches": "patches"}
    }
  }
}
```

Patching a file: targets versus `FILE`
--------------------------------------

Let's start by patching the source file `definitions.units`. While,
conceptionally, we want to patch a third-party source file, we do *not*
modify the sources. The workspace root is a git tree and stay like this.
Instead, we remember that we specify *targets* and the definition of a
target is looked up in the targets file; only if not defined there, it
is implicitly considered a source target and taken from the target root.
So we will define a *target* named `definitions.units` to replace the
original source file.

Let's first generate the patch. As we're already referring to source
files as targets, we have to provide a targets file already; we start
with the empty object and refine it later.

``` sh
$ echo {} > TARGETS.units
$ just-mr install -o . definitions.units
INFO: Performing repositories setup
INFO: Found 4 repositories to set up
INFO: Setup finished, exec ["just","install","-C","...","-o",".","definitions.units"]
INFO: Requested target is [["@","units","","definitions.units"],{}]
INFO: Analysed target [["@","units","","definitions.units"],{}]
INFO: Discovered 0 actions, 0 trees, 0 blobs
INFO: Building [["@","units","","definitions.units"],{}].
INFO: Processed 0 actions, 0 cache hits.
INFO: Artifacts can be found in:
        /tmp/work/./definitions.units [0f24a321694aab5c1d3676e22d01fc73492bee42:342718:f]
$ cp definitions.units definitions.units.orig
$ # interactively edit definitions.units
$ echo -e "/German units\n+2a\narea_soccerfield 105 m * 68 m\narea_saarland 2570 km^2\n.\nw\nq" | ed definitions.units
342718
# A few German units as currently in use.
342772
$ mkdir -p files
$ echo {} > files/TARGETS
$ diff -u definitions.units.orig definitions.units > files/definitions.units.diff
$ rm definitions.units*
```

Our rules conveniently contain a rule `["patch", "file"]` to patch a
single file, and we already created the patch. The only other input
missing is the source file. So far, we could refer to it as
`"definitions.units"` because there was no target of that name, but now
we're about to define a target with that very name. Fortunately, in
target files, we can use a special syntax to explicitly refer to a
source file of the current module, even if there is a target with the
same name: `["FILE", null, "definition.units"]`. The syntax requires the
explicit `null` value for the current module, despite the fact that
explicit file references are only allowed for the current module; in
this way, the name is a list of length more than two and cannot be
confused with a top-level module called `FILE`. So we add this target
and obtain as `TARGETS.units` the following.

``` {.jsonc srcname="TARGETS.units"}
{ "definitions.units":
  { "type": ["@", "rules", "patch", "file"]
  , "src": [["FILE", ".", "definitions.units"]]
  , "patch": [["@", "patches", "", "definitions.units.diff"]]
  }
}
```

Analysing `"definitions.units"` we find our defined target which
contains an action output. Still, it looks like a patched source file;
the new artifact is staged to the original location. Staging is also
used in the action definition, to avoid magic names (like file names
starting with `-`), in-place operations (all actions must not modify
their inputs) and, in fact, have a fixed command line.

``` sh
$ just-mr analyse definitions.units --dump-actions -
INFO: Performing repositories setup
INFO: Found 4 repositories to set up
INFO: Setup finished, exec ["just","analyse","-C","...","definitions.units","--dump-actions","-"]
INFO: Requested target is [["@","units","","definitions.units"],{}]
INFO: Result of target [["@","units","","definitions.units"],{}]: {
        "artifacts": {
          "definitions.units": {"data":{"id":"ac620477c30dc79701cdda95ec97a06f12251b6f","path":"patched"},"type":"ACTION"}
        },
        "provides": {
        },
        "runfiles": {
          "definitions.units": {"data":{"id":"ac620477c30dc79701cdda95ec97a06f12251b6f","path":"patched"},"type":"ACTION"}
        }
      }
INFO: Actions for target [["@","units","","definitions.units"],{}]:
[
  {
    "command": ["sh","./run_patch.sh"],
    "env": {
      "PATH": "/bin:/usr/bin"
    },
    "input": {
      "orig": {
        "data": {
          "file_type": "f",
          "id": "0f24a321694aab5c1d3676e22d01fc73492bee42",
          "size": 342718
        },
        "type": "KNOWN"
      },
      "patch": {
        "data": {
          "path": "definitions.units.diff",
          "repository": "patches"
        },
        "type": "LOCAL"
      },
      "run_patch.sh": {
        "data": {
          "file_type": "f",
          "id": "85786bc8f6aeac0db3be48f8ce336f906e1d78a0",
          "size": 93
        },
        "type": "KNOWN"
      }
    },
    "output": ["patched"]
  }
]
$
```

Building `"definitions.units"` we find out that the patch applied correctly

``` sh
$ just-mr build definitions.units -P definitions.units | grep -A 5 'German units'
INFO: Performing repositories setup
INFO: Found 4 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","definitions.units","-P","definitions.units"]
INFO: Requested target is [["@","units","","definitions.units"],{}]
INFO: Analysed target [["@","units","","definitions.units"],{}]
INFO: Discovered 1 actions, 0 trees, 1 blobs
INFO: Building [["@","units","","definitions.units"],{}].
INFO: Processed 1 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        definitions.units [763f3289422c296057e142f61be190ee6bef049a:342772:f]
# A few German units as currently in use.
#

area_soccerfield 105 m * 68 m
area_saarland 2570 km^2
zentner                 50 kg
$
```

Globbing source files: `"GLOB"`
-------------------------------

Next, we collect all `.units` files. We could simply do this by
enumerating them in a target.

``` {.jsonc srcname="TARGETS.units"}
...
, "data-draft": { "type": "install", "deps": ["definitions.units", "currency.units"]}
...
```

In this way, we get the desired collection of one unmodified source file
and the output of the patch action.

``` sh
$ just-mr analyse data-draft
INFO: Performing repositories setup
INFO: Found 4 repositories to set up
INFO: Setup finished, exec ["just","analyse","-C","...","data-draft"]
INFO: Requested target is [["@","units","","data-draft"],{}]
INFO: Result of target [["@","units","","data-draft"],{}]: {
        "artifacts": {
          "currency.units": {"data":{"file_type":"f","id":"ac6da8afaac0f34e114e123e4ab3a41e59121b10","size":14707},"type":"KNOWN"},
          "definitions.units": {"data":{"id":"ac620477c30dc79701cdda95ec97a06f12251b6f","path":"patched"},"type":"ACTION"}
        },
        "provides": {
        },
        "runfiles": {
          "currency.units": {"data":{"file_type":"f","id":"ac6da8afaac0f34e114e123e4ab3a41e59121b10","size":14707},"type":"KNOWN"},
          "definitions.units": {"data":{"id":"ac620477c30dc79701cdda95ec97a06f12251b6f","path":"patched"},"type":"ACTION"}
        }
      }
$
```

The disadvantage, however, that we might miss newly added `.units` files
if we update and upstream added new files. So we want all source files
that have the respective ending. The corresponding source reference is
`"GLOB"`. A glob expands to the *collection* of all *sources* that are
*files* in the *top-level* directory of the current module and that
match the given pattern. It is important to understand this in detail
and the rational behind it.

 - First of all, the artifact (and runfiles) map has an entry for each
   file that matches. In particular, targets have the option to define
   individual actions for each file, like `["CC", "binary"]` does for
   the source files. This is different from `"TREE"` where the artifact
   map contains a single artifact that happens to be a directory. The
   tree behaviour is preferable when the internals of the directory
   only matter for the execution of actions and not for analysis; then
   there are less entries to carry around during analysis and
   action-key computation, and the whole directory is "reserved" for
   that tree avoid staging conflicts when latter adding entries there.
 - As a source reference, a glob expands to explicit source files;
   targets having the same name as a source file are not taken into
   account. In our example, `["GLOB", null, "*.units"]` therefore
   contains the unpatched source file `definitions.units`. In this way,
   we avoid any surprises in the expansion of a glob when a new source
   file is added with a name equal to an already existing target.
 - Only files are considered for matching the glob. Directories are
   ignored.
 - Matches are only considered at the top-level directory. In this way,
   only one directory has to be read during analysis; allowing deeper
   globs would require traversal of subdirectories requiring larger
   cost. While the explicit `"TREE"` reference allows recursive
   traversal, in the typical use case of the respective workspace root
   being a `git` root, it is actually cheap; we can look up the `git`
   tree identifier without traversing the tree. Such a quick look up
   would not be possible if matches had to be selected.

So, `["GLOB", null, "*.units"]` expands to all the relevant source
files; but we still want to keep the patching. Most rules, like
`"install"`, disallow staging conflicts to avoid accidentally ignoring a
file due to conflicting name. In our case, however, the dropping of the
source file in favour of the patched one is deliberate. For this, there
is the rule `["data", "overlay"]` taking the union of the artifacts of
the specified targets, accepting conflicts and resolving them in a
latest-wins fashion. Keep in mind, that our target fields are list, not
sets. Looking at the definition of the rule, one finds that it is simply
a `"map_union"`. Hence we refine our `"data-draft"` target into the target
`"data"` reading

``` {.jsonc srcname="TARGETS.units"}
...
, "data":
  { "type": ["@", "rules", "data", "overlay"]
  , "deps": [["GLOB", null, "*.units"], "definitions.units"]
  }
...
```

The result of the analysis on this target, of course, remains the same.

Finishing the example: binaries from globbed sources
----------------------------------------------------

The source-code organisation of units is pretty simple. All source and
header files are in the top-level directory. As the header files are not
in a directory of their own, we can't use a tree, so we use a glob,
which is fine for the private headers of a binary. For the source files,
we have to have them individually anyway. So our first attempt of
defining the binary is as follows.

``` {.jsonc srcname="TARGETS.units"}
...
, "units-draft":
  { "type": ["@", "rules", "CC", "binary"]
  , "name": ["units"]
  , "private-ldflags": ["-lm"]
  , "pure C": ["YES"]
  , "srcs": [["GLOB", null, "*.c"]]
  , "private-hdrs": [["GLOB", null, "*.h"]]
  }
...
```

The result basically work and shows that we have 5 source files in
total, giving 5 compile and one link action.

``` sh
$ just-mr build units-draft
INFO: Performing repositories setup
INFO: Found 4 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","units-draft"]
INFO: Requested target is [["@","units","","units-draft"],{}]
INFO: Analysed target [["@","units","","units-draft"],{}]
INFO: Discovered 6 actions, 1 trees, 0 blobs
INFO: Building [["@","units","","units-draft"],{}].
INFO (action:f9426e7a0c3525618ead3787872e843c86f12dd2):
     Stderr of command: ["cc","-I","work","-isystem","include","-c","work/strfunc.c","-o","work/strfunc.o"]
     work/strfunc.c:109:8: warning: extra tokens at end of #endif directive [-Wendif-labels]
       109 | #endif NO_STRSPN
           |        ^~~~~~~~~
INFO: Processed 6 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        units [40cdc2a9fa6f06004bbf290014519ba21f122e7d:124488:x]
$
```

To keep the build clean, we want to get rid of the warning. Of course,
we could simply set an appropriate compiler flag, but let's do things
properly and patch away the underlying reason. To do so, we first create
a patch.

``` sh
$ just-mr install -o . strfunc.c
INFO: Performing repositories setup
INFO: Found 4 repositories to set up
INFO: Setup finished, exec ["just","install","-C","...","-o",".","strfunc.c"]
INFO: Requested target is [["@","units","","strfunc.c"],{}]
INFO: Analysed target [["@","units","","strfunc.c"],{}]
INFO: Discovered 0 actions, 0 trees, 0 blobs
INFO: Building [["@","units","","strfunc.c"],{}].
INFO: Processed 0 actions, 0 cache hits.
INFO: Artifacts can be found in:
        /tmp/work/./strfunc.c [e2aab4b825fa2822ccf33746d467a4944212abb9:2201:f]
$ cp strfunc.c strfunc.c.orig
$ echo -e "109\ns|N|// N\nw\nq" | ed strfunc.c
2201
#endif NO_STRSPN
#endif // NO_STRSPN
2204
$ diff strfunc.c.orig strfunc.c > files/strfunc.c.diff
$ rm strfunc.c*
$
```

Then we amend our `"units"` target.

``` {.jsonc srcname="TARGETS.units"}
...
, "units":
  { "type": ["@", "rules", "CC", "binary"]
  , "name": ["units"]
  , "private-ldflags": ["-lm"]
  , "pure C": ["YES"]
  , "srcs": ["patched srcs"]
  , "private-hdrs": [["GLOB", null, "*.h"]]
  }
, "patched srcs":
  { "type": ["@", "rules", "data", "overlay"]
  , "deps": [["GLOB", null, "*.c"], "strfunc.c"]
  }
, "strfunc.c":
  { "type": ["@", "rules", "patch", "file"]
  , "src": [["FILE", ".", "strfunc.c"]]
  , "patch": [["@", "patches", "", "strfunc.c.diff"]]
  }
...
```

Building the new target, 2 actions have to be executed: the patching,
and the compiling of the patched source file. As the patched file still
generates the same object file as the unpatched file (after all, we only
wanted to get rid of a warning), the linking step can be taken from
cache.

``` sh
$ just-mr build units
INFO: Performing repositories setup
INFO: Found 4 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","units"]
INFO: Requested target is [["@","units","","units"],{}]
INFO: Analysed target [["@","units","","units"],{}]
INFO: Discovered 7 actions, 1 trees, 1 blobs
INFO: Building [["@","units","","units"],{}].
INFO: Processed 7 actions, 5 cache hits.
INFO: Artifacts built, logical paths are:
        units [40cdc2a9fa6f06004bbf290014519ba21f122e7d:124488:x]
$
```

To finish the example, we also add a default target (using that, if no
target is specified, `just` builds the lexicographically first target),
staging artifacts according to the usual conventions.

``` {.jsonc srcname="TARGETS.units"}
...
, "": {"type": "install", "dirs": [["units", "bin"], ["data", "share/units"]]}
...
```

Then things work as expected

``` sh
$ just-mr install -o /tmp/testinstall
INFO: Performing repositories setup
INFO: Found 4 repositories to set up
INFO: Setup finished, exec ["just","install","-C","...","-o","/tmp/testinstall"]
INFO: Requested target is [["@","units","",""],{}]
INFO: Analysed target [["@","units","",""],{}]
INFO: Discovered 8 actions, 1 trees, 1 blobs
INFO: Building [["@","units","",""],{}].
INFO: Processed 8 actions, 8 cache hits.
INFO: Artifacts can be found in:
        /tmp/testinstall/bin/units [40cdc2a9fa6f06004bbf290014519ba21f122e7d:124488:x]
        /tmp/testinstall/share/units/currency.units [ac6da8afaac0f34e114e123e4ab3a41e59121b10:14707:f]
        /tmp/testinstall/share/units/definitions.units [763f3289422c296057e142f61be190ee6bef049a:342772:f]
$ /tmp/testinstall/bin/units 'area_saarland' 'area_soccerfield'
        * 359943.98
        / 2.7782101e-06
$
```
