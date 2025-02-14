% JUST-LOCK CONFIG(5) | File Formats Manual

NAME
====

just-lock-config - The format of the input configuration used by
**`just-lock`**(1)

DESCRIPTION
===========

In order for the **`just-lock`**(1) tool to generate a repository
configuration file usable by the **`just-mr`**(1) launcher, it requires its own
configuration file describing how that resulting configuration should be
obtained.

The file is read as JSON. Any other serialization describing the same
JSON object is equivalent. It is assumed that in JSON objects keys occur
at most once; it is implementation defined how repetitions of the same
key are treated.

Repository import description objects
-------------------------------------

One of the main functionalities of the **`just-lock`**(1) tool is to import
dependencies from other Just projects, as described in their repositories
configuration file. From each such project, one or more repositories can be
imported (with their respective transitive dependencies).
A *repository import description* is a JSON object describing one such
repository to be imported.

The following fields are supported:

 - *`"repo"`* has a string value defining the name of the repository to be
   imported. This entry is optional.

 - *`"alias"`* has a string value defining the new name under which the
   imported repository should be found in the output configuration. This entry
   is optional. If not provided, the value for the key `"repo"` ***must*** exist
   and its value is used instead.
  
 - *`"map"`* has as value a JSON object with string key-value pairs defining a
   mapping  between repository names that would be brought in by this import
   (possibly transitively) and already imported repositories. This mapping can
   be used to avoid additional duplications of repositories from multiple
   imports. This entry is optional.

 - *`"pragma"`* has as value a JSON object. Currently, it supports the key
   *`"absent"`* with a boolean value; if this field evaluates to `true`, it
   informs that the imported repository and all transitive repositories imported
   as a consequence should have the `{"absent": true}` pragma added to their
   description in the output configuration. This entry is optional.

Source objects
--------------

A *source* provides information about an operation that the **`just-lock`**(1)
tool can perform in order to extend an initial repository description stub and
obtain the output repository configuration. In most cases, this operation
involves importing repositories from other Just projects, but a more general
operation exists as well.

Sources are given as JSON objects for which the string value to the mandatory
key *`"source"`* defines a supported type. Each source type informs which other
fields are available. Currently, the supported source types are *`"git"`*,
*`"file"`*, *`"archive"`*, and *`"git tree"`*.

### *`"git"`*

It defines an import operation of one or more dependencies from a Just project
under Git version control.

The following fields are supported:

 - *`"source"`* defines the current *source* type. This entry is mandatory.

 - *`"repos"`* has as value a JSON list where each entry is a
   *repository import description*. This entry is mandatory. An empty list is
   treated as if the current *source* object is missing.

 - *`"url"`* has a string value defining the URL of the Git repository. This
   entry is mandatory.

 - *`"branch"`* has a string value providing the name of the branch to consider
   from the Git repository. This entry is mandatory.

 - *`"commit"`* has a string value providing the hash of the commit to be used.
   This entry is optional. If provided, it has to be specified in hex encoding
   and it must be a commit in the branch specified by the corresponding field.
   If not provided, the `HEAD` commit of that branch is used.

 - *`"mirrors"`* has as value a JSON list of strings defining alternative
   locations for the Git repository to be used if the given URL fails to provide
   it. This entry is optional.

 - *`"inherit env"`* has as value a JSON list which will be recorded as the
   value for the `"inherit env"` key in the output configuration for all
   imported `"git"`-type repositories
   (see **`just-mr-repository-config`**(5)). This entry is optional.

 - *`"as plain"`* has a boolean value. If the field evaluates to `true`, it
   informs **`just-lock`**(1) to consider the foreign repository configuration
   to be the canonical one for a single repository. This can be useful if the
   Git repository does not have a repository configuration or should be imported
   as-is, without dependencies. This entry is optional.

 - *`"config"`* has a string value defining the relative path of the foreign
   repository configuration file to be considered from the Git repository. This
   entry is optional. If not provided and the `"as plain"` field does not
   evaluate to `true`, **`just-lock`**(1) will search for a configuration file
   in the same locations as **`just-mr`**(1) does when invoked with
   **`--norc`** in the root directory of the Git repository.

### *`"file"`*

It defines an import operation of one or more dependencies from a Just project
present as a local checkout.

The following fields are supported:

 - *`"source"`* defines the current *source* type. This entry is mandatory.

 - *`"repos"`* has as value a JSON list where each entry is a
   *repository import description*. This entry is mandatory. An empty list is
   treated as if the current *source* object is missing.

 - *`"path"`* has a string value defining the path to the local checkout. This
   entry is mandatory.

 - *`"as plain"`* has a boolean value. If the field evaluates to `true`, it
   informs **`just-lock`**(1) to consider the foreign repository configuration
   to be the canonical one for a single repository. This can be useful if the
   Git repository does not have a repository configuration or should be imported
   as-is, without dependencies. This entry is optional.

 - *`"config"`* has a string value defining the relative path of the foreign
   repository configuration file to be considered from the Git repository. This
   entry is optional. If not provided and the `"as plain"` field does not
   evaluate to `true`, **`just-lock`**(1) will search for a configuration file
   in the same locations as **`just-mr`**(1) does when invoked with
   **`--norc`** in the root directory of the Git repository.

### *`"archive"`*

It defines an import operation of one or more dependencies from an archived
Just project.

The following fields are supported:

 - *`"source"`* defines the current *source* type. This entry is mandatory.

 - *`"repos"`* has as value a JSON list where each entry is a
   *repository import description*. This entry is mandatory. An empty list is
   treated as if the current *source* object is missing.

 - *`"fetch"`* has a string value defining the URL of the archive. This entry is
   mandatory.

 - *`"type"`* has a string value providing a supported archive type. This entry
   is mandatory. Available values are:

    - `"tar"`: a tarball archive

    - `"zip"`: a zip-like archive

 - *`"content"`* has a string value providing the hash of the archive. This
   entry is optional. If provided, it has to be specified in hex encoding and it
   must be the Git blob identifier of the archive content. If not provided, it
   is computed based on the fetched archive.

 - *`"subdir"`* has a string value providing the relative path to the sources
   root inside the archive. This entry is optional. If missing, the root
   directory of the unpacked archive is used.

 - *`"sha256"`*
 - *`"sha512"`* have string values providing respective checksums for the
   archive. These entries are optional. If provided, they have to be specified
   in hex encoding and they will be checked for the fetched archive.

 - *`"mirrors"`* has as value a JSON list of strings defining alternative
   locations for the archive to be used if the given URL fails to provide it.
   This entry is optional.

 - *`"as plain"`* has a boolean value. If the field evaluates to `true`, it
   informs **`just-lock`**(1) to consider the foreign repository configuration
   to be the canonical one for a single repository. This can be useful if the
   archived repository does not have a configuration file or should be imported
   as-is, without dependencies. This entry is optional.

 - *`"config"`* has a string value defining the relative path of the foreign
   repository configuration file to be considered from the unpacked archive
   root. This entry is optional. If not provided and the `"as plain"` field does
   not evaluate to `true`, **`just-lock`**(1) will search for a configuration
   file in the same locations as **`just-mr`**(1) does when invoked with
   **`--norc`** in the root directory of the unpacked archive.

### *`"git tree"`*

It defines an import operation of one or more dependencies from a Just project
given as the result of running a command. This can be used, for example, to
import projects found under a version control system other than Git.

The following fields are supported:

 - *`"source"`* defines the current *source* type. This entry is mandatory.

 - *`"repos"`* has as value a JSON list where each entry is a
   *repository import description*. This entry is mandatory. An empty list is
   treated as if the current *source* object is missing.

 - *`"cmd"`* provides a list of strings forming a command that, when executed in
   an empty directory (anywhere in the file system), creates the tree of the
   source Just project to use for the import. This entry is optional. One and
   only one of the fields `"cmd"` and `"cmd gen"` must be provided.

 - *`"cmd gen"`* provides a list of strings forming a command that, when
   executed in an empty directory (anywhere in the file system), prints to
   stdout a string giving a JSON serialization of a valid input for the field
   `"cmd"` to be used. This entry is optional. One and only one of the fields
   `"cmd"` and `"cmd gen"` must be provided.

 - *`"env"`* provides a map of envariables to be set for executing the
   command and the command generator, if given. This entry is optional.

 - *`"inherit env"`* provides a list of variables to be inherited from the
   environment `just-lock` is called within, if set there. This entry is
   optional.

 - *`"subdir"`* has a string value providing the relative path to the sources
   root inside the generated tree. This entry is optional. If missing, the root
   directory of the generated tree is considered.

 - *`"as plain"`* has a boolean value. If the field evaluates to `true`, it
   informs **`just-lock`**(1) to consider the foreign repository configuration
   to be the canonical one for a single repository. This can be useful if the
   Git repository does not have a repository configuration or should be imported
   as-is, without dependencies. This entry is optional.

 - *`"config"`* has a string value defining the relative path of the foreign
   repository configuration file to be considered from the Git repository. This
   entry is optional. If not provided and the `"as plain"` field does not
   evaluate to `true`, **`just-lock`**(1) will search for a configuration file
   in the same locations as **`just-mr`**(1) does when invoked with
   **`--norc`** in the root directory of the Git repository.

The just-lock configuration format
----------------------------------

The configuration format is structured as a JSON object. It is a superset of
the **`just-mr-repository-config`**(5), which is extended by two additional
fields. Specifically, the following fields are supported:

 - *`"main"`* has the syntax and semantics as described in
   **`just-mr-repository-config`**(5).

 - *`"repositories"`* has the syntax and semantics as described in
   **`just-mr-repository-config`**(5).
 
 - *`"imports"`* is a JSON list with each entry a *source* object.

 - *`"keep"`* is a JSON list of strings defining the global names of
   repositories to be kept, together with the `"main"` repository, in the
   output configuration during the deduplication step of **`just-lock`**(1).

Additional keys
---------------

Any JSON object described in this format might have additional keys
besides the ones mentioned. The current strategy of **`just-lock`**(1) is
to accept and ignore them. Users should be aware that future versions of
this format might give specific meanings to these extra keys.

See also
========

**`just-lock`**(1),
**`just-mr`**(1),
**`just-mr-repository-config`**(5)
