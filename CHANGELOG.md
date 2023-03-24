## Release `1.1.0` (UNRELEASED)

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
