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
| DEBUG | map, anything logically false | null |
| TOOLCHAIN_CONFIG["FAMILY"] | gnu, clang, unknown | unknown |
| TOOLCHAIN_CONFIG["BUILD_STATIC"] | true, false | false |

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
can be found in `etc/repos.in.json`. This file also specifies, in
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
env PACKAGE=YES LOCALBASE=/usr python3 ${SRCDIR}/bin/bootstrap.py ${SRCDIR} ${BUILDDIR}
```

If some dependencies should nevertheless be built from source (typically
taking the necessary archives from a specified distdir) those can be
specified in the `NON_LOCAL_DEPS` variable which, if set, has to contain
a JSON list.

# Installing `just-mr`

In order to set up multi-repository configurations, usually the tool `just-mr`
is used. It is also a useful launcher for `just`.

This tool can be obtained by building the target `["", "installed just-mr"]`.
That target can be built using the previously bootstrapped `just` together with
the cache directory `.just`, the repository configuration `repo-conf.json`, and
the build configuration `build-conf.json` left by the bootstrapping process in
its build directory. This makes use of already existing cache entries and the
same dependencies (typically the ones provided by the system for package builds)
as for building `just`.

```sh
${BUILDDIR}/out/bin/just install \
  --local-build-root ${BUILDDIR}/.just \
  -C ${BUILDDIR}/repo-conf.json \
  -c ${BUILDDIR}/build-conf.json \
  -o ${BUILDDIR}/out/ 'installed just-mr'
```

# Installing `just-import-git`

The file `bin/just-import-git.py` is a useful Python script that allows quick
generation of a multi-repository build configuration file from a simpler
template for projects with dependencies provided by Git repositories.

It is recommended to make this script available in your `$PATH` as
`just-import-git`. Running it requires, of course, a Python3 interpreter.

# Installing `just-deduplicate-repos`

The file `bin/just-deduplicate-repos.py` is a useful Python script that
removes duplicates from a multi-repository configuration by merging
indistinguishable repositories.

It is recommended to make this script available in your `$PATH` as
`just-deduplicate-repos`. Running it requires, of course, a Python3 interpreter.
