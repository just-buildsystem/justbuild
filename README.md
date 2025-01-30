# Justbuild

*justbuild* is a generic build system supporting multi-repository
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
  - [Getting Started](doc/tutorial/getting-started.md)
  - [Hello World](doc/tutorial/hello-world.md)
  - [Third-party dependencies](doc/tutorial/third-party-software.md)
  - [Debugging](doc/tutorial/debugging.md)
  - [Tests](doc/tutorial/tests.md)
  - [Targets versus `FILE`, `GLOB`, and `TREE`](doc/tutorial/target-file-glob-tree.md)
  - [Ensuring reproducibility](doc/tutorial/rebuild.md)
  - [Using protobuf](doc/tutorial/proto.md)
  - [Running linters](doc/tutorial/lint.md)
  - [How to create a single-node remote execution service](doc/tutorial/just-execute.org)
  - [Dependency management using Target-level Cache as a Service](doc/tutorial/just-serve.md)
  - [Cross compiling and testing cross-compiled targets](doc/tutorial/cross-compiling.md)
  - [Computed roots](doc/tutorial/computed.md)

## Documentation

- [Overview](doc/concepts/overview.md)
- [Build Configurations](doc/concepts/configuration.md)
- [Multi-Repository Builds](doc/concepts/multi-repo.md)
- [Expression Language](doc/concepts/expressions.md)
- [Built-in Rules](doc/concepts/built-in-rules.md)
- [User-Defined Rules](doc/concepts/rules.md)
- [Documentation Strings](doc/concepts/doc-strings.md)
- [Cache Pragma and Testing](doc/concepts/cache-pragma.md)
- [Anonymous Targets](doc/concepts/anonymous-targets.md)
- [Target-Level Caching](doc/concepts/target-cache.md)
- [Target-Level Caching as a Service](doc/concepts/service-target-cache.md)
- [Garbage Collection](doc/concepts/garbage.md)
- [Symbolic links](doc/concepts/symlinks.md)
- [Execution properties](doc/concepts/execution-properties.md)
- [Computed roots](doc/concepts/computed-roots.md)
