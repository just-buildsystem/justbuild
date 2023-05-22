# Contributing

## Preparing changes


Larger changes, as well as changes breaking backwards compatibility
require a design document describing the planned change. It should
carefully discuss the objective of the planned feature, possible
alternatives, as well as the transition plan. This document has to
be agreed upon and committed to the repository first.

For all changes, remember to also update the documentation and
add appropriate test coverage. For code to be accepted, all tests
must pass; the two global test suites are `["@", "just tests", "", "ALL"]`
and `["@", "just tests", "test", "bootstrap-test"]` where the
latter needs an increased action time out. Code is formatted with
`clang-format` and linted with `clang-tidy`; the corresponding
configuration files can be found in the top-level directory of
this repository.

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
