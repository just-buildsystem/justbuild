# Justbuild

Justbuild is a generic build system supporting multi-repository
builds. A peculiarity of the tool is the separation between global
names and physical location on the one hand, and logical paths
used for actions and installation on the other hand (sometimes referred to as
"staging"). The language-specific information to translate high-level
concepts (libraries, binaries) into individual compile action is
taken from user-defined rules described by functional expressions.

## Getting Started

* The most simple way to build the `just` binary from scratch
  is `python3 ./bin/bootstrap.py`. For more details see the
  [installation guide](INSTALL.md).

* Tutorial
  - [Getting Started](doc/tutorial/getting-started.org)
  - [Hello World](doc/tutorial/hello-world.org)
  - [Third party dependencies](doc/tutorial/third-party-software.org)
  - [Tests](doc/tutorial/tests.org)
  - [Targets versus `FILE`, `GLOB`, and `TREE`](doc/tutorial/target-file-glob-tree.org)
  - [Ensuring reproducibility](doc/tutorial/rebuild.org)
  - [Using protobuf](doc/tutorial/proto.org)
  - [How to create a single-node remote execution service](doc/tutorial/just-execute.org)

## Documentation

- [Overview](doc/concepts/overview.org)
- [Build Configurations](doc/concepts/configuration.org)
- [Multi-Repository Builds](doc/concepts/multi-repo.org)
- [Expression Language](doc/concepts/expressions.org)
- [Built-in Rules](doc/concepts/built-in-rules.org)
- [User-Defined Rules](doc/concepts/rules.org)
- [Documentation Strings](doc/concepts/doc-strings.org)
- [Cache Pragma and Testing](doc/concepts/cache-pragma.org)
- [Anonymous Targets](doc/concepts/anonymous-targets.org)
- [Target-Level Caching](doc/concepts/target-cache.org)
- [Garbage Collection](doc/concepts/garbage.org)
