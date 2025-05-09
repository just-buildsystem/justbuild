# Tree Overlays


The underlying idea of a tree object is that it is an opaque object,
that can be passed around as a single artifact. Trees can be obtained
as a directory output of an action or as explicit reference to a
source directory. Using a tree rather than a collection of individual
files can be useful for reserving a whole directory for headers
for a particular library in order to avoid future conflicts, or if
the individual outputs of an action are not known in advance. The
latter happens often when calling foreign build systems to build
a third-party library or even toolchain; still, usually those
outputs can be used as opaque trees using appropriate staging.

There are, however, a few examples where opaque trees have to be
combined into a single one, e.g., if there is an external requirement
that certain files be staged flatly in a single directory.

To also support those rare use cases, `just` supports in-memory
actions to compute the overlay of two trees, optionally rejecting
conflicts instead of resolving them in a latest-wins way. Those
actions can be requested in user-defined rules, as well as by the
built-in rules `tree_overlay` and `disjoint_tree_overlay` (where
the latter causes a build error if the trees cannot be overlayed
in a conflict-free way).

Here we demonstrate the way these rules work on an artificial
example which we start from scratch.

``` sh
$ touch ROOT
```

We simply work locally, so our `repos.json` is trivial.

``` {.jsonc srcname="repos.json"}
{"repositories": {"": {"repository": {"type": "file", "path": "."}}}}
```

As a replacement for a foreign-build-system call, consider a target
having a single tree artifact as output, with contents heavily
depending on the configuration.

``` {.jsonc srcname="TARGETS"}
{ "foo":
  { "type": "generic"
  , "arguments_config": ["FOO_BINS"]
  , "out_dirs": ["bin"]
  , "cmds":
    [ "mkdir -p bin"
    , { "type": "join"
      , "$1":
        [ "for tool in "
        , {"type": "join_cmd", "$1": {"type": "var", "name": "FOO_BINS"}}
        , " ; do echo \"foo binary ${tool}\" > bin/\"${tool}\""
        , " ; chmod 755 bin/\"${tool}\""
        , " ; done"
        ]
      }
    ]
  }
}
```

So, depending on the configuration the output tree has different entries.

``` sh
$ just-mr build -D '{"FOO_BINS": ["version", "upload", "download"]}' -p foo
INFO: Performing repositories setup
INFO: Found 1 repositories involved
INFO: Setup finished, exec ["just","build","-C","...","-D","{\"FOO_BINS\": [\"version\", \"upload\", \"download\"]}","-p"]
INFO: Requested target is [["@","","","foo"],{"FOO_BINS":["version","upload","download"]}]
INFO: Analysed target [["@","","","foo"],{"FOO_BINS":["version","upload","download"]}]
INFO: Discovered 1 actions, 0 tree overlays, 0 trees, 0 blobs
INFO: Building [["@","","","foo"],{"FOO_BINS":["version","upload","download"]}].
INFO: Processed 1 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        bin [8cd3ecc03f0ba26d9e104e52b40f88a0bc5a84b9:105:t]
{
  "download": "[f270834a3411ba7d9e6fab59a8d93e1fbd6e55a3::x]",
  "upload": "[af38d5c40e8828c67d0031fce42da86b68f56182::x]",
  "version": "[a263fff2a3b94429878303875861fd93bcdbe248::x]"
}
$ just-mr build -D '{"FOO_BINS": ["version", "ci", "co", "rlog"]}' -p foo
...
INFO: Processed 1 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        bin [eaee8451fe929904016b73c73f466f534e248c3d:127:t]
{
  "ci": "[eb8e3ec5baca0f16da7fe8b200181bde123c11bf::x]",
  "co": "[1cb77df2638381460b338882e2942b3eb0a09975::x]",
  "rlog": "[157a0fca577e8717d68920f76a998ab1b398594d::x]",
  "version": "[a263fff2a3b94429878303875861fd93bcdbe248::x]"
}
```

Now, assume we have another such target.

``` {.jsonc srcname="TARGETS"}
...
, "bar":
  { "type": "generic"
  , "arguments_config": ["BAR_BINS"]
  , "out_dirs": ["bin"]
  , "cmds":
    [ "mkdir -p bin"
    , { "type": "join"
      , "$1":
        [ "for tool in "
        , {"type": "join_cmd", "$1": {"type": "var", "name": "BAR_BINS"}}
        , " ; do echo \"bar binary ${tool}\" > bin/\"${tool}\""
        , " ; chmod 755 bin/\"${tool}\""
        , " ; done"
        ]
      }
    ]
  }
...
```

Both targets produce a tree `bin` instead of a collection of files
within the directory `bin`. Therefore, an overlay at analysis time
could only result in one or the other directory. However, we overlay
the results at build time.

``` {.jsonc srcname="TARGETS"}
...
, "both": {"type": "tree_overlay", "deps": ["foo", "bar"]}
, "both-noconflict": {"type": "disjoint_tree_overlay", "deps": ["foo", "bar"]}
...
```

If the entries do not conflict, in both cases, we get the union of the files.
``` sh
$ just-mr build -D '{"FOO_BINS": ["ci", "co"], "BAR_BINS": ["up", "down"]}' -P bin both
...
INFO: Processed 2 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
         [98e5f0eed05bf887a6c9df7616b7d83323acf677:30:t]
INFO: 'bin' not a direct logical path of the specified target; will take subobject 'bin' of ''
{
  "ci": "[eb8e3ec5baca0f16da7fe8b200181bde123c11bf::x]",
  "co": "[1cb77df2638381460b338882e2942b3eb0a09975::x]",
  "down": "[53aa524d39aff972c976bfd729a3fd26c5d364fd::x]",
  "up": "[3bfadc230e82da61f056e1d9acd854298f0b19c3::x]"
}
$ just-mr build -D '{"FOO_BINS": ["ci", "co"], "BAR_BINS": ["up", "down"]}' -P bin both-noconflict
...
INFO: Processed 2 actions, 2 cache hits.
INFO: Artifacts built, logical paths are:
         [98e5f0eed05bf887a6c9df7616b7d83323acf677:30:t]
INFO: 'bin' not a direct logical path of the specified target; will take subobject 'bin' of ''
{
  "ci": "[eb8e3ec5baca0f16da7fe8b200181bde123c11bf::x]",
  "co": "[1cb77df2638381460b338882e2942b3eb0a09975::x]",
  "down": "[53aa524d39aff972c976bfd729a3fd26c5d364fd::x]",
  "up": "[3bfadc230e82da61f056e1d9acd854298f0b19c3::x]"
}
```

In case of a conflict, however, the targets differ. The first one, `both`, will
overlay the files in a latest-wins fashion, whereas the second will fail when
handling the overlay action.

``` sh
$ just-mr build -D '{"FOO_BINS": ["version", "ci", "co"], "BAR_BINS": ["version", "up", "down"]}' -P bin/version both
...
INFO: Processed 2 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
         [2a26b3dc1774df55eb1f1d9a865611a413204ab2:30:t]
INFO: 'bin/version' not a direct logical path of the specified target; will take subobject 'bin/version' of ''
bar binary version
$ just-mr build -D '{"FOO_BINS": ["version", "ci", "co"], "BAR_BINS": ["version", "up", "down"]}' both-noconflict || :
INFO: Performing repositories setup
INFO: Found 1 repositories involved
INFO: Setup finished, exec ["just","build","-C","...","-D","{\"FOO_BINS\": [\"version\", \"ci\", \"co\"], \"BAR_BINS\": [\"version\", \"up\", \"down\"]}","both-noconflict"]
INFO: Requested target is [["@","","","both-noconflict"],{"BAR_BINS":["version","up","down"],"FOO_BINS":["version","ci","co"]}]
INFO: Analysed target [["@","","","both-noconflict"],{"BAR_BINS":["version","up","down"],"FOO_BINS":["version","ci","co"]}]
INFO: Discovered 2 actions, 1 tree overlays, 2 trees, 0 blobs
INFO: Building [["@","","","both-noconflict"],{"BAR_BINS":["version","up","down"],"FOO_BINS":["version","ci","co"]}].
ERROR (action:aec6a6e881d3c3884420bee1330fbe9702ad3a7af11ee7f21fb59f88d6ba8f38):
     Tree-overlay computation failed:
     While merging the trees:
       - [394c2bd3bf4a07f8e22f6e73c2adcb03f28349df:30:t]
       - [e83b879ac94530f49855004138ea96df758b14d1:30:t]
     While merging the trees:
       - [7a5ed39406620e7b844788589f92fa4c17361c19:0:t]
       - [28891f0d46f5053861ac36bc7da18285e65ed267:0:t]
     Naming conflict detected at path "version":
       - [a263fff2a3b94429878303875861fd93bcdbe248:0:x]
       - [e9ebc93e1f91db5fab1fde7d22d8c6f59afab9f1:0:x]
ERROR (action:aec6a6e881d3c3884420bee1330fbe9702ad3a7af11ee7f21fb59f88d6ba8f38):
     Failed to execute tree-overlay action.
     requested by
      - [["@","","","both-noconflict"],{"BAR_BINS":["version","up","down"],"FOO_BINS":["version","ci","co"]}]#0
     inputs were
      - 394c2bd3bf4a07f8e22f6e73c2adcb03f28349df:30:t
      - e83b879ac94530f49855004138ea96df758b14d1:30:t
ERROR: Build failed.
```
