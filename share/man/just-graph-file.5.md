% JUST GRAPH FILE(5) | File Formats Manual

NAME
====

just graph file - The format of the action graph used by **`just`**(1)

DESCRIPTION
===========

The file is read as JSON. Any other serialization describing the same
JSON object is equivalent. We assume, that in JSON objects, each key
occurs at most once; it is implementation defined how repetitions of the
same key are treated.

Artifacts and their serialization
---------------------------------

There are four different kind of artifacts. The serialization of the
artifact is always a JSON object with two keys: *`"type"`* and
*`"data"`*. The value for *`"type"`* is always on of the strings
*`"LOCAL"`*, *`"KNOWN"`*, *`"ACTION"`*, or *`"TREE"`*. The value for
*`"data"`* is a JSON object with keys depending on which type the
artifact is of.

 - *`"LOCAL"`* artifacts refer to source files in a logical repository.
   The describing data are
    - the *`"repository"`*, given as its global name, and
    - the *`"path"`*, given as path relative to the root of that
      repository.
 - *`"KNOWN"`* artifacts are described by the hash of their content. The
   describing data are
    - the *`"file_type"`*, which is a one-letter string,
       - *`"f"`* for regular, non-executable files,
       - *`"x"`* for executable files, or
       - *`"t"`* for trees.
    - the *`"id"`* specifying the (applicable) hash of the file given as
      its hexadecimal encoding, a string, and
    - the *`"size"`* of the artifact, a number, in bytes.
 - *`"ACTION"`* artifacts are the outputs of actions. The defining data
   are
    - the *`"id"`*, a string with the name of the action, and
    - the *`"path"`*, specifying the path of this output artifact,
      relative to the root of the action directory
 - *`"TREE"`* artifacts refer to trees defined in the action graph. The
   defining data are
    - the *`"id"`*, a string with the name of the tree.

Actions and their serialization
-------------------------------

Actions are given by the data described in the following sections. For
each item a key is associated and the serialization of the action is a
JSON object mapping those keys to the respective serialization of the
values. For some data items default values are given; if the value for
the respective item equals the default, the respective key-value pair
may be omitted in the serialization.

 - *`"command"`* specifies the command to be executed. It is a non-empty
   list of strings that forms the argument vector; the first entry is
   taken as program. This key is mandatory.
 - *`"env"`* specifies the environment variables the command should be
   executed with. It is given as map from strings to strings. The
   default is the empty map.
 - *`"input"`* describes the files available to the action. The action
   must not modify them in any way. They are specified as map from paths
   to artifacts (the latter serialized as described). Paths have to be
   relative paths and are taken relative to the working directory of the
   action. The default is the empty map.
 - *`"output"`* describes the files the action is supposed to generate,
   if any. It is given as a list of strings, each specifying a file name
   by a path relative to the working directory of the action. The
   default is the empty list. However, every action has to produce some
   form of output, so if *`"output"`* is empty, *`"output_dirs"`* cannot
   be empty.
 - *`"output_dirs"`* describes the directory output of the action, if
   any. It is given as a list of strings, each specifying the a
   directory name by a path relative to the working directory of the
   action. The *`"output_dirs"`* may also specify directories from which
   individual files are specified as *`"output"`*. The default value for
   *`"output_dirs"`* is the empty list. However, every action to produce
   some form of output, so if *`"output_dirs"`* is empty, *`"output"`*
   cannot be empty.
 - *`"may_fail"`* can either be *`null`* or a string. If it is a string,
   the build should be continued, even if that action returns a non-zero
   exit state. If the action returns a non-zero exit code, that string
   should be shown to the user in a suitable way (e.g., printing on the
   console). Otherwise (i.e., if no *`"may_fail"`* string is given), the
   build should be aborted if the action returns a non-zero exit code.
   The default for *`"may_fail"`* is *`null`*, i.e., abort on non-zero
   exit code.
 - *`"no_cache"`* is a boolean. If *`true`*, the action should never be
   cached, not even on success. The default is *`false`*.

The graph format
----------------

The action graph is given by a JSON object.

 - The value for the key *`"blobs"`* is a list of strings. Those strings
   should be considered known; they might (additionally to what was
   agreed ahead of time as known) referred to as *`"KNOWN"`* artifacts.

 - The value for the key *`"trees"`* is a JSON object, mapping the names
   of trees to their definition. The definition of a tree is JSON object
   mapping paths to artifacts (serialized in the way described earlier).
   The paths are always interpreted as relative paths, relative to the
   root of the tree. It is not a requirement that a new tree is defined
   for every subdirectory; if a path contains the hierarchy separator,
   which is slash, then implicitly a subdirectory is present, and all
   path going through that subdirectory contribute to its content. It
   is, however, an error, if the a path is a prefix of another one (with
   the comparison done in the canonical form of that path).

 - The value for the key *`"actions"`* is a JSON object, mapping names
   of actions to their respective definition (serialized as JSON).

Additional keys
---------------

Any JSON object described here might have additional keys on top of the
ones described. Implementations reading the graph have to accept and
ignore those. Implementations writing action-graph files should be aware
that a future version of this file format might give a specific meaning
to those extra keys.

Graphs written by **`just`**(1) have the additional key *`"origins"`* in
each action. The value is a list of all places where this action was
requested (so often, but not always, the list has length 1). Each such
place is described by a JSON object with the following keys.

 - *`"target"`* The target in which the action was requested. It is
   given as a list, either a full qualified named target given as
   *`"@"`* followed by global repository name, module, and target name,
   or an anonymous target, given by *`"#"`* followed by a hash of the
   rule binding and the node name.
 - *`"subtask"`* The running number, starting from 0, of the action, as
   given by the (deterministic) evaluation order of he defining
   expression for the rule that defined the target.
 - *`"config"`* The effective configuration for that target, a JSON
   object.

See also
========

**`just`**(1)
