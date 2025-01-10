% JUST-MR REPOSITORY CONFIG(5) | File Formats Manual

NAME
====

just-mr-repository-config - The format of the repository config used by
**`just-mr`**(1)

DESCRIPTION
===========

In order for the **`just-mr`**(1) tool to generate a repository
configuration file usable by the **`just`**(1) multi-repository build
system, it requires a configuration file describing repositories and
their dependencies.

The file is read as JSON. Any other serialization describing the same
JSON object is equivalent. It is assumed that in JSON objects keys occur
at most once; it is implementation defined how repetitions of the same
key are treated.

Workspace root description
--------------------------

A repository's *workspace root description* provides information about
the location of source files. It can be an explicit description, given
as a JSON object, or an implicit one, given as the global name of
another repository, from which the workspace root can be inferred.

Explicit workspace roots can be of several types, distinguishable by the
value of the key *`"type"`* in the JSON object. Depending on this value,
other fields in the object are also supported.

### *`"file"`*

It defines as workspace root a directory on the file system.

The following fields are supported:

 - *`"path"`* provides the root directory containing the source files.
   This entry is mandatory.

### *`"archive"`* / *`"zip"`*

They define as workspace root a remote archive. The only difference
between the two types is the nature of the archive: a tarball (preferably
compressed) in the case of *`"archive"`*, or a compressed zip or 7zip file
in the case of *`"zip"`*.

The following fields are supported:

 - *`"content"`* provides the Git blob hash of the archive file. This
   has to be specified in hex encoding. This entry is mandatory.

 - *`"fetch"`* specifies the URL to the remote archive. This entry is
   mandatory.

 - *`"distfile"`* provides an alternative name for the archive file.
   This entry is optional. If missing, the basename of the fetch URL is
   used.

 - *`"mirrors"`* is an optional list of alternative locations to try to
   fetch from if contacting the main fetch location fails. This entry is
   optional.

 - *`"sha256"`*,

 - *`"sha512"`* provide optional checksum hashes in order to verify the
   integrity of the remote site archive. These have to be provided in
   hex encoding. These checks are only performed if the archive file is
   actually downloaded from the (potentially untrusted) network and not
   already available locally.

 - *`"subdir"`* specifies the subdirectory withing the unpacked archive.
   This entry is optional. If missing, the root directory of the archive
   is used.

### *`"foreign file"`*

Define a root as consisting of single file with given content at
a specific name with specified executable bit.

The following fields are supported.

- *`"content"`*, *`"fetch"`*, *`"distfile"`*, *`"mirrors"`*, *`"sha256"`*,
  and *`"sha512"`* specify the file content in the same way as they specify
  the archive content for an *`"archive"`* repository.

- *`"name"`* specifies the name the content should have in the defined root.
  It has to be a plain file name without implicitly specified sudirs. This
  field is mandatory.

- *`"executable"`* is a boolean indicating whether the fetched file should
  be provided with the executable bit. Defaults to `false`.

### *`"git"`*

It defines as workspace root a part of a Git repository.

The following fields are supported:

 - *`"repository"`* provides the URL of the Git repository. This entry
   is mandatory. Note that only URLs starting with `/`, `./`, or `file://`
   are considered file URLs.

 - *`"commit"`* contains the commit hash. This has to be specified in
   hex encoding. This entry is mandatory.

 - *`"branch"`* provides the branch name, with the promise that it
   contains the aforementioned commit. This entry is mandatory.

 - *`"mirrors"`* is an optional list of alternative locations to try to
   fetch from if contacting the main repository fails. This entry is
   optional.

 - *`"subdir"`* specifies the subdirectory containing the distribution
   files. This entry is optional. If missing, the root directory of the
   Git repository is used.

 - *`"inherit env"`* provides a list of variables. When `just-mr`
   shells out to `git`, those variables are inherited from the
   environment `just-mr` is called within, if set there.

### *`"git tree"`*

It defines as workspace root as a fixed `git` tree, given by the
corresponding tree identifier. If that tree is not known already to
`just-mr`, a specified command will be executed in a fresh directory
that is expected to produce the given tree somewhere below the
working directory.

This type of root is the way builds against sources versioned in
arbitrary version-control systems can be carried out. The command
then would be a call to the version control system requesting an
export at a particular version.

As the root is already fully determined by the specified `git` tree
identifier, the corresponding action need not be fully isolated.
In fact, to check out a repository it might be necessary to provide
credentials (which do not matter for the checked-out tree, but
differ from user to user). To support this, the description of the
root can specify environment variables to inherit from the ambient
environment. E.g., `SSH_AUTH_SOCK` can be specified here to support
`ssh`-based authentication.

The following fields are supported:

 - *`"id"`* provides the Git tree identifier. This entry is mandatory.

 - *`"cmd"`* provides a list of strings forming a command which promises
   that, when executed in an empty directory (anywhere in the file
   system), creates in that directory a directory structure containing
   the promised Git tree (either top-level or in some subdirectory).
   This entry is mandatory.

 - *`"env"`* provides a map of envariables to be set for executing the
   command.

 - *`"inherit env"`* provides a list of variables to be inherited from the
   environment `just-mr` is called within, if set there.

### *`"distdir"`*

It defines as workspace root a directory with the distribution archives
of the specified repositories. Usually this root is realized as a Git
tree in the Git repository in **`just`**'s local build root.

The following fields are supported:

 - *`"repositories"`* provides a list of global names of repositories.
   This entry is mandatory.

### Additional keys

The key *`"pragma"`* is reserved for type-specific repository directives
which alter the workspace root. It is given as a JSON object. The
different workspace roots might support different keys for this object;
unsupported keys are always ignored.

For a *`"file"`* workspace root the pragma key *`"to_git"`* is
supported. If its value is *`true`* then it indicates that the workspace
root should be returned as a Git tree. If the root directory is already
part of a Git repository, its Git tree identifier is used; otherwise,
the workspace root will be realized as a Git tree in the Git repository
in **`just`**'s local build root.

For all workspace roots except *`"distdir"`*, *`"computed"`*,
and *`"tree structure"`*, the pragma key *`"special"`* is
supported. If its value is *`"ignore"`* then it indicates that the workspace
root should ignore all special (i.e., neither file, executable, nor tree)
entries. For a *`"file"`* workspace root or for an *`"archive"`* workspace root
a value of *`"resolve-completely"`* indicates that the workspace root should
resolve all confined relative symbolic links, while a value of
*`"resolve-partially"`* indicates that the workspace root should resolve only
the confined relative upwards symbolic links; for a *`"file"`* workspace root
these two values imply *`"to_git"`* is *`true`*.

For all workspace roots the pragma key *`"absent"`* is supported. If its value
is *`true`* then it indicates that an absent root should be generated, i.e., one
given only by its Git tree without any explicit witnessing repository.

Repository description
----------------------

A *repository description* is defined as a JSON object, containing a
*workspace root description*, directory roots and names for targets,
rules, and expressions files, and bindings to other repositories.

Specifically, the following fields are supported:

 - *`"repository"`* contains a *workspace root description*. This entry
   is mandatory.

 - *`"target_root"`*,

 - *`"rule_root"`*,

 - *`"expression_root"`* define the root directories for the targets,
   rules, and expressions, respectively. If provided, they are passed on
   expanded to the workspace root of the repository named by their
   value.

 - *`"target_file_name"`*,

 - *`"rule_file_name"`*,

 - *`"expression_file_name"`* refer to the name of the files containing
   the targets, rules, and expressions, respectively, located relative
   to the corresponding root directories. These entries are optional.
   If provided, they are passed on as-is.

 - *`"bindings"`* provides a JSON object defining dependencies on other
   repositories. The object's keys are strings defining local repository
   names, while the values are the corresponding global names of those
   repositories. If provided, this entry is passed on as-is.

Repository configuration format
-------------------------------

The repository configuration format is structured as a JSON object. The
following fields are supported:

 - *`"main"`* contains a JSON string that determines which of the
   provided repositories is considered the main repository. This entry
   is optional, and if ommitted, it will be ommitted in the generated
   **`just-repository-config`**.

 - *`"repositories"`* contains a JSON object, where each key is the
   global name of a repository and its corresponding value is the
   *repository description*.

Additional keys
---------------

Any JSON object described in this format might have additional keys
besides the ones mentioned. The current strategy of **`just-mr`**(1) is
to accept and ignore them. Users should be aware that future versions of
this format might give specific meanings to these extra keys.

See also
========

**`just`**(1),
**`just-mr`**(1),
**`just-repository-config`**(5)
