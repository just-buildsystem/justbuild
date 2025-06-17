# Contributing

## Preparing changes


Larger changes, as well as changes breaking backwards compatibility
require a design document describing the planned change. It should
carefully discuss the objective of the planned feature, possible
alternatives, as well as the transition plan. This document has to
be agreed upon and committed to the repository first.

For all changes, remember to also update the documentation and add
appropriate test coverage. For code to be accepted, all tests must
pass; the global test suite is `["@", "just tests", "", "ALL"]`.
Code is formatted with `clang-format` and linted with `clang-tidy`;
the corresponding configuration files can be found in the top-level
directory of this repository. The top-level lint target is
`["@", "lint", "", ""]`. Formatting issues can be fixed by building
and applying the patch `["@", "lint", "", "format.diff"]`; the script
`bin/format-code.sh` does precisely this.

*NOTE:* In order for everyone to use the same version of the linting
tools, the `"lint"` repository
[bootstraps](https://github.com/just-buildsystem/bootstrappable-toolchain)
the required tools; the configuration variable `"TOOLCHAIN_CONFIG"`
is honored. As a consequence, (transitively) depending on the
`"lint"` repository pulls in quite some sources and linting the
first time requires a significant amount of time to build the tools
from first principles. This is also true when calling `bin/format-code.sh`.

Changes should be organized as a patch series, i.e., as a sequence of
small changes that are easy to review, but nevertheless self-contained
in the sense that after each such change, the code builds and the
tests pass; in particular, tests should be rebased to come after
the implementation in the series to avoid spurious errors when
doing `git bisect`. And the end of a patch series, there should be
no "loose ends" like library functions added, but never used.

## Submitting patches

Patches can be sent for review by creating a feature branch in a
clone of the repository and creating a merge request on one of the
`git`-hosting sites hosting this repository.

Alternatively, patches can also be sent via email using `git
format-patch`, `git send-email`. Please use the `--cover-letter`
option and add the description of the patch series in the cover
letter. When sending a revised version, e.g., because of review
comments, please make the new cover letter `In-Reply-To:` the old
over letter

## Review

Each commit has to be reviewed by a core member of the project (a
person with write access to the repository) that is not the author
of the respective commit. The core member who did the review will
also push the commits. Patch series go in as a whole or not at all.
Changes are added fast-forward of the current `master` without a
merge commit, rebasing the patches, if necessary.
