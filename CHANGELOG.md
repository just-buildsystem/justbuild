## Release `1.2.0` (UNRELEASED)

A feature release on top of `1.1.0`, backwards compatible.

### Major new features

- Actions can now define additional execution properties and in
  that way chose a specific remote execution image, as well as a
  factor to scale the time out. This also applies to the built-in
  `generic` rule.
- Relative non-upwards symbolic links are now treated as first-class
  objects. This introduces a new artifact type and allows the free use
  of such symbolic links throughout the build process.

### Other changes

- `just-import-git` now supports an option `--plain` to import a
  repository without dependencies.
- Minor changes to the layout of the local build root; in particular,
  left-over execution directories will eventually get cleaned up
  by garbage collection.
- `just-mr` now supports unpacking tar archives compressed with
  bzip2 and xz.

### Fixes

- Removed potential uses of `malloc` between `fork` and `exec`.
  This removes the risk of deadlocks on certain combinations of
  `C++` standard library and `libc`.
- The link flags for the final linking now can be set via the
  configuration variable `FINAL_LDFLAGS`; in particular, the stack
  size can easily be adapted. The default stack size is now set to
  8M, removing an overflow on systems where the default stack size
  was significantly lower.
- The man pages are now provided as markdown files, allowing to
  potentially reduce the build dependencies to more standard ones.

## Release `1.1.0` (2023-05-19)

A feature release on top of `1.0.0`, backwards compatible.

### Major new features

- new subcommand `just execute` to start a single node execution
  service
- New subcommand `just gc` to clean up no longer needed cache and
  CAS entries
- `just` now supports authentication to remote execution via TLS
  and mutual TLS
- `just-mr` is now available as C++ binary and supports fetching in parallel

### Important changes

- The option `-D` now accumulates instead of ignoring all but the
  latest occurrence. This is an incompatible change of the command
  line, but not affecting the backwards compatibility of the build.

- The option `-L` of `just-mr` now is an alternative name for option
  `--local-launcher` instead of `--checkout-locations`, and thus
  matching its meaning in `just`. This is an incompatible change of
  the command line, but not affecting the backwards compatibility of
  the build.

### Other changes

- `just install` and `just install-cas` now have a new `--remember`
  option ensuring that the installed artifacts are also mirrored in
  local CAS
- `just analyse` now supports a new option `--dump-export-targets`

### Note

There is a regression in `libgit2` versions `1.6.1` up to and
including `1.6.4` with a fix already committed upstream. This
regression makes `just` unusable when built against those versions.
Therefore, the third-party build description for `libgit2` is still
for version `1.5.2`.

## Release `1.1.0~beta2` (2023-05-15)

Second beta release for the upcoming `1.1.0` release; see release
notes there.

### Changes since `1.1.0~beta1`

- fix a race condition in our use of `libgit2`
- a fix in the error handling of git trees
- fixes to the third-party descriptions of our dependencies; in particular,
  the structure of the `export` targets is cleaned up. These changes should
  not affect package builds.
- various minor fixes to documentation and tests

### Note

There is a regression in `libgit2` versions `1.6.1` upto and
including `1.6.4` with a fix already committed upstream. This
regression makes `just` unusable when built against those versions.
Therefore, the third-party build description for `libgit2` is still
for version `1.5.2`.

## Release `1.1.0~beta1` (2023-04-28)

First beta release for the upcoming `1.1.0` release; see release
notes there.

## Release `1.0.0` (2022-12-12)

Initial stable release.

### Important changes since `1.0.0~beta6`

- built-in rule "tree" added
- clean up of user-defined rules for C++
- various documentation improvements

## Release `1.0.0~beta6` (2022-10-16)

### Important changes since `1.0.0~beta5`

- The "configure" built-in rule now evaluates "target". Also,
  a bug in the computation of the effective configuration
  was fixed.
- Option `--dump-vars` added to `just analyse`
- Rule fixes in propagating `ENV`
- Launcher functionality added to `just-mr`
- `just` now takes the lexicographically first repository as default
  if no main repository is specified

## Release `1.0.0~beta5` (2022-10-19)

First public beta version.
