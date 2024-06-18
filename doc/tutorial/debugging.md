Debugging
=========

With *justbuild* striving to ensure that only our own project's code is needed
to be available locally, and not any of our dependencies (especially in
conjunction with our [serve service](./just-serve.md)) or any on-the-fly patches
or generated files, it might seem that debugging is not a straight-forward
endeavor. However, *justbuild* always knows exactly what source files are needed
to build a target and therefore they can be staged locally and made available to
a debugger, such as `gdb(1)`.

In the following we will show how to use the `["CC", "install-with-deps"]` rule
to stage all the needed sources for debugging a program with `gdb(1)`.
As example we use the *hello_world* program employing high-level target caching
from the section on
[*Building Third-party dependencies*](./third-party-software.md), which depends
on the open-source project [fmtlib](https://github.com/fmtlib/fmt).

The debugging process
---------------------

The `TARGETS` file currently contains only the release version of our
*hello-world* program, so we firstly need to add a new target, configured in
debug mode.

``` {.jsonc srcname="TARGETS"}
...
, "helloworld-debug":
  { "type": "configure"
  , "target": "helloworld"
  , "config":
    { "type": "let*"
    , "bindings": [["DEBUG", true]]
    , "body": {"type": "env", "vars": ["DEBUG"]}
    }
  }
...
```

This describes the debug version of *hello-world*, configured by setting the
`"DEBUG"` flag; this also instructs the toolchain, which honors the `"DEBUG"`
flag, to compile all actions with the `"-g"` flag in order to generate debug
information for `gdb(1)`.

To actually collect the artifacts needed to run the debugger, we use the
`["CC", "install-with-deps"]` rule, contained in the `rules-cc` repository.

``` {.jsonc srcname="TARGETS"}
...
, "helloworld-debug staged":
  { "type": ["@", "rules", "CC", "install-with-deps"]
  , "targets": ["helloworld-debug"]
  }
...
```

Now this target can be installed to a location of our choice provided by the
`-o` argument.

``` sh
$ just-mr install "helloworld-debug staged" -o .ext/debug
INFO: Performing repositories setup
INFO: Found 5 repositories to set up
INFO: Setup finished, exec ["just","install","-C","...","helloworld-debug staged","-o",".ext/debug"]
INFO: Requested target is [["@","tutorial","","helloworld-debug install"],{}]
INFO: Analysed target [["@","tutorial","","helloworld-debug install"],{}]
INFO: Export targets found: 0 cached, 1 uncached, 0 not eligible for caching
INFO: Discovered 7 actions, 3 trees, 0 blobs
INFO: Building [["@","tutorial","","helloworld-debug install"],{}].
INFO: Processed 7 actions, 0 cache hits.
INFO: Artifacts can be found in:
        /tmp/tutorial/.ext/debug/bin/helloworld [d88cadc156dc3b9b442fc162f7bc92c86b63d5f8:1570432:x]
        /tmp/tutorial/.ext/debug/include/fmt [3a5cb60e63f7480b150fdef0883d7a76e8a57a00:464:t]
        /tmp/tutorial/.ext/debug/include/greet/greet.hpp [63815ae1b5a36ab29efa535141fee67f3b7769de:53:f]
        /tmp/tutorial/.ext/debug/work/fmt [3a5cb60e63f7480b150fdef0883d7a76e8a57a00:464:t]
        /tmp/tutorial/.ext/debug/work/format.cc [ecb8cc79a6e9ff277db43876a11eccde40814ece:5697:f]
        /tmp/tutorial/.ext/debug/work/greet/greet.cpp [8c56239aabd4e23b9d170333d03f222e6938dcef:115:f]
        /tmp/tutorial/.ext/debug/work/greet/greet.hpp [63815ae1b5a36ab29efa535141fee67f3b7769de:53:f]
        /tmp/tutorial/.ext/debug/work/main.cpp [92f9b55228774b3d3066652253499395d9ebef31:76:f]
        /tmp/tutorial/.ext/debug/work/os.cc [04b4dc506005d38250803cfccbd9fd3b6ab30599:10897:f]
INFO: Backing up artifacts of 1 export targets
```

To now debug the *helloworld* executable, we simply need to switch to the
specified directory and run `gdb(1)`.

``` sh
$ cd .ext/debug
$ gdb bin/helloworld
```

This works out-of-the-box specifically because our tool keeps track of the
logical staging of the artifacts (from direct and transitive dependencies) of
targets, meaning it can easily mirror this staging in the install folder, thus
ensuring a debugger finds all the symbols in the places it looks for by default.
As such, this install directory can also be directly provided to an IDE (such as
VSCode) as search location for debug symbols.

Finally, note that not only the artifacts of the first-party library `greet` get
staged, but also the artifacts of the third-party dependency `fmtlib`. This is
due to the fact that the `"fmtlib"` export target is already configured (see its
`TARGETS` file) to inherit the `"DEBUG"` flag from the environment, meaning that
the `true` value set by the `"helloworld-debug"` target gets honored.
In general, we recommend that export targets always allow the `"DEBUG"` flag
through, specifically to ensure consumers have access to and can build the
target library also in debug mode.
