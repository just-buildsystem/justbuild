# Installing the `just` binary

## Building `just` using an older version of `just-mr` and `just`

If an older installation of `just` is already available, `just`
can simply be built by
```sh
just-mr build
```
This is always guaranteed to work with the latest stable release of `just`.

This will build `just` for Linux on the x86_64 architecture with a dynamic link
dependency on glibc.

### Building `just` for other architectures

First, make sure that the cross-compiler for the desired architecture is
installed and working properly. For example, to build `just` for 64 bit ARM,
specify `arm64` as the target architecture via the `-D` flag:

```sh
just-mr build -D '{"TARGET_ARCH":"arm64"}'
```

The following table describes the most important supported configuration
variables. The full list can be obtained via `just-mr describe`.

|Variable|Supported Values|Default Value for `just`|
|-|:-:|:-:|
| OS | linux | linux |
| ARCH | x86, x86_64, arm, arm64 | x86_64 |
| HOST_ARCH | x86, x86_64, arm, arm64 | *derived from ARCH* |
| TARGET_ARCH | x86, x86_64, arm, arm64 | *derived from ARCH* |
| TOOLCHAIN_CONFIG["FAMILY"] | gnu, clang, unknown | unknown |
| DEBUG | true, false | false |
| BUILD_STATIC_BINARY | true, false | false |

Note that you can choose a different stack size for resulting binaries by
adding `"-Wl,-z,stack-size=<size-in-bytes>"` to variable `"FINAL_LDFLAGS"`
(which has to be a list of strings).

## Bootstrapping `just`

It is also possible to build `just` without having an older binary,
using the `bin/bootstrap.py` script.

### Bootstrapping compiling the dependencies from scratch

If using bundled dependencies is acceptable, the only thing required
are a C++20 compiler with the libraries required by the language
standard (note that, e.g., in Debian `libstdc++-10-dev` is not a
dependency of `clang`) and a Python3 interpreter. By default the bootstrap
script uses the clang compiler. If you also want the bootstrap script to
download the dependencies itself, `wget` is required as well.

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
  with no other processes interfering. The bootstrapped binary has
  path `out/bin/just` relative to that directory.
- A directory where (some of) the archives of the dependencies
  are downloaded ahead of time (defaulting to `.distfiles` in the
  user's home directory). Whenever an archive is needed, it is
  first checked if a file with the basename of the URL exists in
  this directory and has the expected blob id (computed the same
  way as `git` does). Only if this is not the case, fetching from
  the network is attempted.

Additionally, if the environment variable `DEBUG` is set, the second
bootstrap phase is carried out sequentially rather than in parallel.

Moreover, when constructing the build configuration, the scripts
starts with the value of the environment variable `JUST_BUILD_CONF` instead
of the empty object, if this variable is set. One configuration parameter
is the build environment `ENV` that can be used to set an unusual
value of `PATH`, e.g.,
``` sh
env JUST_BUILD_CONF='{"ENV": {"PATH": "/opt/toolchain/bin"}}' python3 ./bin/bootstrap.py
```
Additionally, if `SOURCE_DATE_EPOCH` is set in the build environment, it
is forwarded to the build configuration as well. If, on the other hand,
`CC` or `CXX` are set in the build configuration, those are also used
for the initial steps of the bootstrap procedure. Remember that setting
one of those variables also requires the `TOOLCHAIN_CONFIG["FAMILY"]` to
ensure the proper flags are used (if in doubt, set to `"unknown"`).

In any case, the resulting binary is self contained and can be moved
to an appropriate location in `PATH`.

### Bootstrapping against preinstalled dependencies (package building)

The main task is to ensure all the dependencies are available at
sufficiently compatible versions. The full list of dependencies
can be found in `etc/repos.json`. This file also specifies, in
the `"local_path"` attribute of `"pkg_bootstrap"`, the location
relative to `LOCALBASE` (typically `/usr` or `/usr/local`) that is
taken as root for the logical repository of that dependency. As,
however, library dependencies are taken via `pkg-config`, in most
cases, setting this attribute should not be necessary. The target
files for the dependencies can be found in `etc/import.pkgconfig`.
Possibly different names to be used by `pkg-config` can be adapted
there. If the environment variable `PKG_CONFIG_PATH` is set, the
bootstrap script forwards it to the build so that `pkg-config` can
pick up the correct files.

The build command is the same (with the same positional arguments),
however with the environment variable `PACKAGE` being present
and `LOCALBASE` set accordingly. As package building requires a
predictable location on where to pick up the resulting binary, you
almost certainly want to set the scratch directory.

```sh
env PACKAGE=YES LOCALBASE=/usr python3 ${WRKSRC}/bin/bootstrap.py ${WRKSRC} ${WRKDIR}/just-work
```

If some dependencies should nevertheless be built from source (typically
taking the necessary archives from a specified distdir) those can be
specified in the `NON_LOCAL_DEPS` variable which, if set, has to contain
a JSON list.

# Installing `just-mr`

In order to set up multi-repository configurations, usually the tools `just-mr`
is used. It also a useful launcher for `just`.

This tool is Python3 script located at `bin/just-mr.py` and can simply be put
into an appropriate location in `PATH`.

There is also a compiled version of `just-mr`, which is faster,
works in parallel, and has additional features. It can be obtained
by building the target `["", "installed just-mr"]`. That target
can be built using the bootstrapped `just` and the Python3 script
`bin/just-mr.py`. Alternatively, the bootstrapping process leaves
in its work directory a file `repo-conf.json` with the repository
configuration and a file `build-conf.json` with the build configuration
used. Those can be used to build `just-mr` using the bootstrapped
`just`. This approach is preferable for package building, as the
same dependencies (typically the ones provided by the system) are
used as for building `just.
