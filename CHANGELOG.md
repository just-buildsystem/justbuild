## Release `1.6.0` (UNRELEASED)

A feature release on top of `1.5.0`, backwards compatible.

### New features

- `just-mr` now supports logging of each invocation by setting
  an appropriate configuration in the rc-file. Together with
  the newly-added option `--profile` of `just` this can be used
  to gather statistics on build times, cache hit rates, as well as
  their evolution over time.
- Computing a tree as overlays of other trees was added as a new
  in-memory action.
- The expression language has been extended to contain new
  built-in functions `"zip_with"`, `"zip_map"`.

### Fixes

- `just execute` and `just serve` now create their pid and info
  files atomically; so waiting processes can assume the content
  to be available as soon as the requested file appears on the
  file sytem.
- `just serve` now fetches trees from remote execution in parallel
  and through its local CAS; this fixes a performance issue.
- `just-mr` now also consideres computed roots (as no-op) when
  reporting progress.

## Release `1.5.0` (2025-03-06)

A feature release on top of `1.4.0`, backwards compatible.

### Major new features

- Added two new root types: `"computed"` and `"tree structure"`.
  This allows to define roots as the artifacts of an export target
  of an earlier-defined content-fixed repository, as well as the
  underlying tree structure of an earlier-defined content-fixed
  root. Both new root types are themselves content fixed.

### New features

- A new tool `just-lock` has been added that allows to define
  `just-mr` repository configurations out of an abstract configuration
  defining base repositories and a sequence of imports.
- An option `-p` was added to the building subcommands to show the
  unique artifact (if there is precisely one) on stdout.
- The checkout-locations file now additionally allows to specify
  extra environment variables to inherit.
- `just add-to-cas` now supports the `--resolve-special` option, which
  defines how special entries (e.g., symlinks) are to be handled when
  adding directories to CAS.
- `just serve` accepts a new subkey `"client address"` for the key
  `"execution endpoint"` in the configuration file. It informs the
  `serve` instance that the client will access the remote-execution
  endpoint via the given `"client address"` and not using the one
  listed in the subkey `"address"`. This feature allows to position
  `just serve` next to the remote-execution endpoint behind some
  form of address translation.

### Fixes

- `just-import-git` now correctly inherits pragmas for imported
  file-type repositories during description rewrite.
- `just-mr` repository garbage collection now properly removes
  no longer needed directories.
- The "generic" rule now properly detects staging conflicts.
- Fixes ensuring proper pointer life time and access check.
- A race condition in the use of `libgit2` was fixed that could
  result in a segmentation fault.
- Git operations are now properly locked against each other, also
  between processes where necessary.
- The Git cache root repository on a `just serve` endpoint is now
  ensured to always exist and be initialized before being operated on.
- `just install-cas` correctly exits with non-zero exit code on
  failure, also if installation to stdout is requested.
- `just traverse` now exits unconditionally after traversal, also
  in case of failure.
- `just-mr` properly enforces that repository `subdir` entries are
  non-upwards relative paths.
- The local api correctly handles not-found blobs, even in the absence
  of a local git api.
- For remote execution, the server capability `max_batch_total_size_bytes`
  is now correctly honored, if announced by the server.
- Missing entries in the documentation have been added.


### Changes since `1.5.0~beta2`

- Fixed how `just-import-git` and `just-lock` handle the transitively
  implied base repositories of computed roots; the lack of properly
  handling indirections led to crashes even if computed roots where
  not used at all.
- A case was fixed where special entries where not ignored properly,
  even though this was requested.
- Unnecessary verbosity reduced.
- Updated dependencies.
- Documentation extended.

## Release `1.5.0~beta2` (2025-02-28)

Second beta release for the upcoming `1.5.0` release; see release
notes there.

### Changes since `1.5.0~beta1`

- New configuration option `"client address"` for `just serve`.
- `just-lock` now fetches and clones repositories in parallel.
- Blob content is not any more kept in memory unecessarily at
  various places.
- Various internal clean up of the code base.

## Release `1.5.0~beta1` (2025-02-24)

First beta release for the upcoming `1.5.0` release; see release
notes there.

## Release `1.4.0` (2024-11-04)

A feature release on top of `1.3.0`, backwards compatible with
respect to rule language, build description, repository description,
and wire protocols. However, the internal representation in local
build root has changed; it is therefore recommended to remove the
local build root on upgrade.

### New features

- `just serve` now also works together with a compatible remote-execution
  endpoint. This uses an extended version of the serve protocol, so
  both, `just-mr` and `just serve` need to be at the new version.
- User-defined rules, as well as the built-in rule `"generic"` can
  now specify a subdirectory in which an action is to be executed.
- `just-mr` now supports garbage collection for repository roots
  via the `gc-repo` subcommand. This follows the same two-generation
  approach as garbage collection for the cache-CAS pair; in
  other words, everything is cleaned up that was not used since
  the last call to `gc-repo`. To accommodate this, the layout in
  the local build root had to be changed. The directory `git` as
  well as `*-map` directories are now located in the subdirectory
  `repositories/generation-0`. On upgrade those have to be manually
  moved there if they should be continued to be used; removing the
  whole local build root is, of course, also a valid upgrade path,
  however losing the whole cache. Not doing anything on upgrade
  will not lead to an inconsistent state; however, the directories
  at the old location will not be used anymore while still using
  disk space.
- The expression language has been extended to contain quote
  and quasi-quote expressions, as well as new built-in functions
  `"from_subdir"`, `"nub_left"`.

### Fixes

- The built-in rule `"generic"` now properly enforces that the
  obtained outputs form a well-formed artifact stage; a conflicting
  arrangement of artifacts was possilbe beforehand.
- The built-in expression functions `"join"` and `"join_cmd"`
  now properly enforce that the argument is a list of strings.
  So far, they used to accept a single string, treating it as a
  singleton list.
- A bug was fixed that cased `just serve` to fail with an internal
  error when building against ignore-special roots.
- `just` now accurately reports internal errors that occurred on
  the serve endpoint.
- Target-level cache entries are only written if all export targets
  depended upon are also written to or found in cache; previously,
  it was assumed that all export targets not analysed locally
  were local cache hits, an assumption that no longer holds in
  the presence of serve endpoints. This fixes a cache consistency
  problem if the same remote-execution endpoint is used both, with
  and without a serve endpoint.
- A race condition in reconstructing executables from large CAS
  has been removed that could lead to an open file descriptor being
  kept alive for too long, resulting EBUSY failures of actions
  using this binary.
- Internal code clean up, reducing memory footprint, in particular
  for simultaneous upload of a large number of blobs.
- Avoidence of duplicate requests and performance improvements when
  synchronizing artifacts with another CAS.
- Dependencies have been updated to also build with gcc 14.
- Portability improvements of the code by not relying on implementation
  details of the compiler.
- Local execution no longer has the requirement that there exist
  no more files with identical content than the hardlink limit of
  the underlying file system.
- Inside action descriptions, paths are always normalized; this improves
  compatibility with existing remote-execution implementations.
- The size of large object entries has been reduced. The cache
  and CAS must be cleaned up since stable versions before `1.4.0`
  cannot use the new format.
- The way of storing intermediate keys of the action cache has
  been changed. The cache must be cleaned up since stable versions
  before `1.4.0` cannot use the new format.
- Various improvements to the tests: dispatching of the summary
  action is now possible, tests are independent of a .just-mrrc
  file the user might have in their home directory
- Various improvements of the documentation.

## Release `1.4.0~beta1` (2024-10-30)

First beta release for the upcoming `1.4.0` release; see release
notes there.

## Release `1.3.0` (2024-05-08)

A feature release on top of `1.2.0`, backwards compatible.

### Major new features

- New subcommand `just serve` to start a target-level caching service,
  as described in the corresponding design document.
- `just-mr` is able to back up and retrieve distribution files
  from a remote execution endpoint. This simplifies usage in an
  environment with restricted internet access.
- `just execute` now supports blob splitting as new RPC call.
  `just install` uses this call to reduce traffic if the remote-execution
  endpoint supports blob splitting and the `--remember` option is given.
  In this way, traffic from the remote-execution endpoint can be reduced
  when subsequently installing artifacts with only small local
  differences.

### Other changes

- New script `just-deduplicate-repos` to avoid blow up of the
  `repos.json` in the case of chained imports with common dependencies.
- New subcommand `add-to-cas` to add files and directories to the local
  CAS and optionally also copy them to the remote-execution endpoint.
- The built-in `"generic"` rule now supports an argument `"sh -c"`,
  allowing to specify the invocation of the shell (defaulting to
  `["sh", "-c"]`).
- `just describe` also shows the values of the implicit dependencies.
- `just-mr` supports a new form of root, called `"foreign file"`.
- When `just-mr` executes the action to generate the desired tree of a
  `"git tree"` repository, it can be specified that certain variables
  of the environment can be inherited.
- The just-mr rc file now supports a field `"rc files"` to include
  other rc files given by location objects; in particular, it is
  possible to include rc files committed to the workspace.
- Support for fetching archives from FTP and TFTP was added to `just-mr`
  if it was built with bundled curl. For package builds, libcurl has
  enabled whatever the distro considers suitable.
- The `gc` subcommand supports an option `--no-rotate` to carry
  out only local clean up. Part of that local clean up, that is
  also done as part of a full `gc`, is splitting large files. Note
  that stable versions before `1.3.0` cannot use those split files.
  Hence a downgrade after a `gc` with `1.3.0` (or higher) requires
  cleaning of cache and CAS.
- The expression language has been extended and, in particular,
  allows indexed access to an array (basically using it as a tuple)
  and a generic form of assertion (to report user errors).
- The `analyse` subcommand supports a new flag `--dump-result` to dump
  the analysis result to a file or stdout (if `-` is given).

### Fixes

- The cache key used for an export target is now based on the
  export target itself rather than that of the exported target. The
  latter could lead to spurious cache hits, but only in the case
  where the exported target was an explicit file reference, and a
  regular target with the same name existed as well. Where the new
  cache keys would overlap with the old ones, they would refer to
  the same configured targets. However, we used the fact that we
  changed the target cache key to also clean up the serialization
  format to only contain the JSON object describing repository,
  target, and effective configuration, instead of a singleton list
  containing this object. Therefore, old and new cache keys do not
  overlap at all. In particular, no special care has to be taken
  on upgrading or downgrading. However, old target-level cache
  entries will not be used leading potentially to rebuilding of
  some targets.
- Garbage collection now honors the dependencies of target-level
  caches entries on one another. When upgrading in place, this only
  applies for target-level cache entries written initially after
  the upgrade.
- The taintedness of `"configure"` targets is now propagated
  correctly in analysis.
- It is no longer incorrectly assumed that every `git` URL not
  starting with `ssh://`, `http://`, nor `https://` is a file on the
  local disk. Now, only URLs starting with `/`, `./`, or `file://`
  are considered file URLs. File URLs, as well as URLs starting
  with `git://`, `http://`, or `https://`, are handled by `just-mr`
  using `libgit2`; for every other URL, `just-mr` shells out to
  `git` for fetching and the URL is passed to `git` unchanged.
- Improved portability and update of the bundled dependencies.
- Various minor improvements and typo fixes in the documentation.
- Fixed a race condition in the task queue that could cause (with
  probability roughly 1e-5) a premature termination of the queue
  resulting in spurious analysis failures without explanation (despite
  "failed to analyse target").
- Fixed a race condition in an internal cache of `just execute`
  used for keeping track of running operations.
- The built-in rule `"install"` now properly enforces that the
  resulting stage is well-formed, i.e., without tree conflicts.
- Local execution and `just execute` now correctly create empty
  directories if they are part of the action's input.
- Fixed overwrite of existing symlinks in the output directory
  when using subcommands `install` and `install-cas`.
- The format for target-cache shards was changed to a canonical form.
  The new and old formats do not overlap, therefore the correctness
  of the builds is not affected. In particular, no special care has
  to be taken on upgrading or downgrading. However, some target-level
  cache entries will not be used leading potentially to rebuilding of
  some targets.
- The expression `"disjoint_map_union"` did not verify disjointness
  in all cases; this is fixed now.
- The command line option `"--remote-execution-property"` can be
  repeated multiple times to list all the properties, but only the
  last one was retained. This is fixed now.

### Changes since `1.3.0~beta1`

- The `["CC/pkgconfig", "system_library"]` rule now propagates
  `ENV` correctly, fixing the build on systems where the default
  paths pulled in by `env` do not contain `cat`.
- In case of a build failure, the description of the failing action
  in the error message is now more verbose, including the environment.
- Various minor fixes in the documentation.

## Release `1.3.0~beta1` (2024-05-02)

First beta release for the upcoming `1.3.0` release; see release
notes there.

## Release `1.2.0` (2023-08-25)

A feature release on top of `1.1.0`, backwards compatible.

### Major new features

- Actions can now define additional execution properties and in
  that way chose a specific remote execution image, as well as a
  factor to scale the time out. This also applies to the built-in
  `generic` rule. Additionally, the remote-execution endpoint can
  be dispatched based on the remote-execution properties using
  the `--endpoint-configuration` argument.
- Relative non-upwards symbolic links are now treated as first-class
  objects. This introduces a new artifact type and allows the free use
  of such symbolic links throughout the build process.
- `just-mr` can now optionally resolve symlinks contained in archives.

### Other changes

- `just-import-git` now supports an option `--plain` to import a
  repository without dependencies.
- Minor changes to the layout of the local build root; in particular,
  left-over execution directories, as well as left-over temporary
  directories of `just-mr`, will eventually get cleaned up by
  garbage collection.
- `just-mr` now supports unpacking tar archives compressed with
  bzip2, xz, lzip, and lzma.
- The option `-P` of `build` and `install-cas` can be used to
  inspect parts of a tree.
- `just-mr` now supports unpacking 7zip archives (with default
  compression) when provided as `"zip"` type repositories.
- The configuration variable `COMPILER_FAMILY` is replaced by the more
  flexible `TOOLCHAIN_CONFIG`, an object which may contain the field
  `FAMILY`. From now on, this object is used to set the compiler family
  (e.g., for GNU, set `{"TOOLCHAIN_CONFIG":{"FAMILY":"gnu"}}`).

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
- `just-mr` now correctly performs a forced add in order to stage
  all entries in a Git repository. Previously it was possible for
  entries to be skipped inadvertently in, e.g., imported archives
  if `gitignore` files were present.
- Temporary files generated by `just execute` are now created inside
  the local build root.
- `just install-cas` now correctly handles `--raw-tree` also for
  remote-execution endpoints.
- `just install-cas` now, like `just install`, removes an existing
  destination file before installing instead of overwriting.
- Only actions with exit code 0 that generated all required outputs
  are taken from cache, instead of all actions with exit code 0.
  This only affects remote execution, as purely local build didn't
  cache actions with incomplete outputs.

### Changes since `1.2.0~beta3`

- Only actions with exit code 0 that generated all required outputs
  are taken from cache, instead of all actions with exit code 0.
  This only affects remote execution, as purely local build didn't
  cache actions with incomplete outputs.
- Splitting off libraries from the main binary targets to simplify
  cherry-picking future fixes from the head development branch.
- Improvements of the bundled dependency descriptions.
- Update of documentation.

## Release `1.2.0~beta3` (2023-08-22)

Third beta release for the upcoming `1.2.0` release; see release
notes there.

### Changes since `1.2.0~beta2`

- Update and clean up of bundled dependency descriptions
- Improvement of documentation

## Release `1.2.0~beta2` (2023-08-18)

Second beta release for the upcoming `1.2.0` release; see release
notes there.

### Changes since `1.2.0~beta1`

- Clean up of the internal build description of bundled dependencies.
- Clean up of the internal rules, in particular renaming of
  implicit dependency targets.
- Various documentation improvements.

## Release `1.2.0~beta1` (2023-08-16)

First beta release for the upcoming `1.2.0` release; see release
notes there.

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
