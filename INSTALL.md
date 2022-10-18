# Installing the `just` binary

## Building `just` using an older version of `just-mr` and `just`

```sh
just-mr build
```

This will build `just` for Linux on the x86_64 architecture with a dynamic link
dependency on glibc.

### Building `just` for other architectures

First, make sure that the cross-compiler for the desired architecture is
installed and working properly. For example, to build `just` for 64 bit ARM,
specify `arm64` as the target architecture via the `-D` flag:

```sh
just-mr build -D '{"TARGET_ARCH":"arm64"}'
```

A complete list of variables honored by our build rules is provided in the
following table:

|Variable|Supported Values|Default Value for `just`|
|-|:-:|:-:|
| OS | linux | linux |
| ARCH | x86, x86_64, arm, arm64 | x86_64 |
| HOST_ARCH | x86, x86_64, arm, arm64 | *derived from ARCH* |
| TARGET_ARCH | x86, x86_64, arm, arm64 | *derived from ARCH* |
| COMPILER_FAMILY | gnu, clang | clang |
| DEBUG | true, false | false |
| BUILD_STATIC_BINARY | true, false | false |

## Bootstrapping `just`

It is also possible to build `just` without having an older binary,
using the `bin/bootstrap.py` script.

### Bootstrapping compiling the dependencies from scratch

If using bundled dependencies is acceptable, the only thing required
are a C++20 compiler with the libraries required by the language
standard and a Python3 interpreter. If you also want the bootstrap
script to download the dependencies itself, `wget` is required as well.

In this case, the command is simply
```sh
python3 ./bin/bootstrap.py
```

The script also takes optionally the following positional arguments (in
the given order, i.e., specifying one argument requires the ones
before to be present as well).
- The directory of the source location (defaulting to the current
  working directory). Specifying that directory allows calling the
  script from a different location. It should be noted that the
  script is written in such a way that the source is not modified.
- The scratch directory (defaulting to python's `tempfile.mkdtemp()`).
  The script assumes it can use that directory entirely on its own
  with no other processes interfering. The bootstraped binary has
  path `out/bin/just` relative to that direcotry.
- A directory where (some of) the archives of the dependecies
  are downloaded ahead of time (defaulting to `.distfiles` in the
  user's home directory). Whenever an archive is needed, it is
  first checked if a file with the basename of the URL exists in
  this directory and has the expected blob id (computed the same
  way as `git` does). Only if this is not the case, fetching from
  the network is attempted.
Additionally, if the environment variable `DEBUG` is set, the second
bootstrap phase is carried out sequentially rather than in parallel.

In any case, the resulting binary is selfcontained and can be moved
to an appropriate location in `PATH`.

### Bootstrapping against preinstalled dependencies (package building)

The main task is to ensure all the dependencies are available at
sufficiently compatible versions. The full list of dependencies
can be found in `etc/repos.json`. This file also specifies, in
the `"local_path"` attribute of `"local_bootstrap"`, the location
relative to `LOCALBASE` (typically `/usr` or `/usr/local`) that
is taken as root for the logical respository of that dependency.
If your distribution prefers to install each package in a separate
directory, you can always take `/` as `LOCALBASE` and adapt these
paths accordingly. The instructions on how to link those dependencies
are stored in the targets files in `etc/import.prebuilt`. Depending
on the packaging, the linking flags might need adaption as well.

The build command is the same (with the same positional arguments),
however with the environment variable `PACKAGE` being present
and `LOCALBASE` set accordingly. As package building requires a
predictable location on where to pick up the resulting binary, you
almost certainly want to set the scratch directory.

```sh
env PACKAGE=YES LOCALBASE=/usr python3 ${WRKSRC}/bin/bootstrap.py ${WRKSRC} ${WRKDIR}/just-work
```
