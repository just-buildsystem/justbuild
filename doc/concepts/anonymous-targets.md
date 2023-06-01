Anonymous targets
=================

Motivation
----------

Using [Protocol buffers](https://github.com/protocolbuffers/protobuf)
allows to specify, in a language-independent way, a wire format for
structured data. This is done by using description files from which APIs
for various languages can be generated. As protocol buffers can contain
other protocol buffers, the description files themselves have a
dependency structure.

From a software-engineering point of view, the challenge is to ensure
that the author of the description files does not have to be aware of
the languages for which APIs will be generated later. In fact, the main
benefit of the language-independent description is that clients in
various languages can be implemented using the same wire protocol (and
thus capable of communicating with the same server).

For a build system that means that we have to expect that language
bindings at places far away from the protocol definition, and
potentially several times. Such a duplication can also occur implicitly
if two buffers, for which language bindings are generated both use a
common buffer for which bindings are never requested explicitly. Still,
we want to avoid duplicate work for common parts and we have to avoid
conflicts with duplicate symbols and staging conflicts for the libraries
for the common part.

Our approach is that a "proto" target only provides the description
files together with their dependency structure. From those, a consuming
target generates "anonymous targets" as additional dependencies; as
those targets will have an appropriate notion of equality, no duplicate
work is done and hence, as a side effect, staging or symbol conflicts
are avoided as well.

Preliminary remark: action identifiers
--------------------------------------

Actions are defined as Merkle-tree hash of the contents. As all
components (input tree, list of output strings, command vector,
environment, and cache pragma) are given by expressions, that can
quickly be computed. This identifier also defines the notion of equality
for actions, and hence action artifacts. Recall that equality of
artifacts is also (implicitly) used in our notion of disjoint map union
(where the set of keys does not have to be disjoint, as long as the
values for all duplicate keys are equal).

When constructing the action graph for traversal, we can drop duplicates
(i.e., actions with the same identifier, and hence the same
description). For the serialization of the graph as part of the analyse
command, we can afford the preparatory step to compute a map from action
id to list of origins.

Equality
--------

### Notions of equality

In the context of builds, there are different concepts of equality to
consider. We recall the definitions, as well as their use in our build
tool.

#### Locational equality ("Defined at the same place")

Names (for targets and rules) are given by repository name, module
name, and target name (inside the module); additionally, for target
names, there's a bit specifying that we explicitly refer to a file.
Names are equal if and only if the respective strings (and the file
bit) are equal.

For targets, we use locational equality, i.e., we consider targets
equal precisely if their names are equal; targets defined at
different places are considered different, even if they're defined
in the same way. The reason we use notion of equality is that we
have to refer to targets (and also check if we already have a
pending task to analyse them) before we have fully explored them
with all the targets referred to in their definition.

#### Intensional equality ("Defined in the same way")

In our expression language we handle definitions; in particular, we
treat artifacts by their definition: a particular source file, the
output of a particular action, etc. Hence we use intensional
equality in our expression language; two objects are equal precisely
if they are defined in the same way. This notion of equality is easy
to determine without the need of reading a source file or running an
action. We implement quick tests by keeping a Merkle-tree hash of
all expression values.

#### Extensional equality ("Defining the same object")

For built artifacts, we use extensional equality, i.e., we consider
two files equal, if they are bit-by-bit identical.
Implementation-wise, we compare an appropriate cryptographic hash.
Before running an action, we built its inputs. In particular (as
inputs are considered extensionally) an action might cause a cache
hit with an intensionally different one.

#### Observable equality ("The defined objects behave in the same way")

Finally, there is the notion of observable equality, i.e., the
property that two binaries behaving the same way in all situations.
As this notion is undecidable, it is never used directly by any
build tool. However, it is often the motivation for a build in the
first place: we want a binary that behaves in a particular way.

### Relation between these notions

The notions of equality were introduced in order from most fine grained
to most coarse. Targets defined at the same place are obviously defined
in the same way. Intensionally equal artifacts create equal action
graphs; here we can confidently say "equal" and not only isomorphic:
due to our preliminary clean up, even the node names are equal. Making
sure that equal actions produce bit-by-bit equal outputs is the realm of
[reproducibe builds](https://reproducible-builds.org/). The tool can
support this by appropriate sandboxing, etc, but the rules still have to
define actions that don't pick up non-input information like the
current time, user id, readdir order, etc. Files that are bit-by-bit
identical will behave in the same way.

### Example

Consider the following target file.

```jsonc
{ "foo":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["echo Hello World > out.txt"]
  }
, "bar":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["echo Hello World > out.txt"]
  }
, "baz":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["echo -n Hello > out.txt && echo ' World' >> out.txt"]
  }
, "foo upper":
  { "type": "generic"
  , "deps": ["foo"]
  , "outs": ["upper.txt"]
  , "cmds": ["cat out.txt | tr a-z A-Z > upper.txt"]
  }
, "bar upper":
  { "type": "generic"
  , "deps": ["bar"]
  , "outs": ["upper.txt"]
  , "cmds": ["cat out.txt | tr a-z A-Z > upper.txt"]
  }
, "baz upper":
  { "type": "generic"
  , "deps": ["baz"]
  , "outs": ["upper.txt"]
  , "cmds": ["cat out.txt | tr a-z A-Z > upper.txt"]
  }
, "ALL":
  { "type": "install"
  , "files":
    {"foo.txt": "foo upper", "bar.txt": "bar upper", "baz.txt": "baz upper"}
  }
}
```

Assume we build the target `"ALL"`. Then we will analyse 7 targets, all
the locationally different ones (`"foo"`, `"bar"`, `"baz"`,
`"foo upper"`, `"bar upper"`, `"baz upper"`). For the targets `"foo"`
and `"bar"`, we immediately see that the definition is equal; their
intensional equality also renders `"foo upper"` and `"bar upper"`
intensionally equal. Our action graph will contain 4 actions: one with
origins `["foo", "bar"]`, one with origins `["baz"]`, one with origins
`["foo upper", "bar upper"]`, and one with origins `["baz
upper"]`. The `"install"` target will, of course, not create any
actions. Building sequentially (`-J 1`), we will get one cache hit. Even
though the artifacts of `"foo"` and `"bar"` and of `"baz"` are defined
differently, they are extensionally equal; both define a file with
contents `"Hello World\n"`.

Anonymous targets
-----------------

Besides named targets we also have additional targets (and hence also
configured targets) that are not associated with a location they are
defined at. Due to the absence of definition location, their notion of
equality will take care of the necessary deduplication (implicitly, by
the way our dependency exploration works). We will call them "anonymous
targets", even though, technically, they're not fully anonymous as the
rules that are part of their structure will be given by name, i.e.,
defining rule location.

### Value type: target graph node

In order to allow targets to adequately describe a dependency structure,
we have a value type in our expression language, that of a (target)
graph node. As with all value types, equality is intensional, i.e.,
nodes defined in the same way are equal even if defined at different
places. This can be achieved by our usual approach for expressions of
having cached Merkle-tree hashes and comparing them when an equality
test is required. This efficient test for equality also allows using
graph nodes as part of a map key, e.g., for our asynchronous map
consumers.

As a graph node can only be defined with all data given, the defined
dependency structure is cycle-free by construction. However, the tree
unfolding will usually be exponentially larger. For internal handling,
this is not a problem: our shared-pointer implementation can efficiently
represent a directed acyclic graph and since we cache hashes in
expressions, we can compute the overall hash without folding the
structure to a tree. When presenting nodes to the user, we only show the
map of identifier to definition, to avoid that exponential unfolding.

We have two kinds of nodes.

#### Value nodes

These represent a target that, in any configuration, returns a fixed
value. Source files would typically be represented this way. The
constructor function `"VALUE_NODE"` takes a single argument `"$1"`
that has to be a result value.

#### Abstract nodes

These represent internal nodes in the dag. Their constructor
`"ABSTRACT_NODE"` takes the following arguments (all evaluated).

 - `"node_type"`. An arbitrary string, not interpreted in any way,
   to indicate the role that the node has in the dependency
   structure. When we create an anonymous target from a node, this
   will serve as the key into the rule mapping to be applied.
 - `"string_fields"`. This has to be a map of strings.
 - `"target_fields"`. These have to be a map of lists of graph
   nodes.

Moreover, we require that the keys for maps provided as
`"string_fields"` and `"target_fields"` be disjoint.

### Graph nodes in `export` targets

Graph nodes are completely free of names and hence are eligible for
exporting. As with other values, in the cache the intensional definition
of artifacts implicit in them will be replaced by the corresponding,
extensionally equal, known value.

However, some care has to be taken in the serialisation that is part of
the caching, as we do not want to unfold the dag to a tree. Therefore,
we take as JSON serialisation a simple dict with `"type"` set to
`"NODE"`, and `"value"` set to the Merkle-tree hash. That serialisation
respects intensional equality. To allow deserialisation, we add an
additional map to the serialisation from node hash to its definition.

### Dependings on anonymous targets

#### Parts of an anonymous target

An anonymous target is given by a pair of a node and a map mapping
the abstract node-type specifying strings to rule names. So, in the
implementation these are just two expression pointers (with their
defined notion of equality, i.e., equality of the respective
Merkle-tree hashes). Such a pair of pointers also forms an
additional variant of a name value, referring to such an anonymous
target.

It should be noted that such an anonymous target contains all the
information needed to evaluate it in the same way as a regular
(named) target defined by a user-defined rule. It is an analysis
error analysing an anonymous target where there is no entry in the
rules map for the string given as `"node_type"` for the
corresponding node.

#### Anonymous targets as additional dependencies

We keep the property that a user can only request named targets. So
anonymous targets have to be requested by other targets. We also
keep the property that other targets are only requested at certain
fixed steps in the evaluation of a target. To still achieve a
meaningful use of anonymous targets our rule language handles
anonymous targets in the following way.

##### Rules parameter `"anonymous"`

In the rule definition a parameter `"anonymous"` (with empty map
as default) is allowed. It is used to define an additional
dependency on anonymous targets. The value has to be a map with
keys the additional implicitly defined field names. It is hence
a requirement that the set of keys be disjoint from all other
field names (the values of `"config_fields"`, `"string_fields"`,
and `"target_fields"`, as well as the keys of the `"implict"`
parameter). Another consequence is that `"config_transitions"`
map may now also have meaningful entries for the keys of the
`"anonymous"` map. Each value in the map has to be itself a map,
with entries `"target"`, `"provider"`, and `"rule_map"`.

For `"target"`, a single string has to be specifed, and the
value has to be a member of the `"target_fields"` list. For
provider, a single string has to be specified as well. The idea
is that the nodes are collected from that provider of the
targets in the specified target field. For `"rule_map"` a map
has to be specified from strings to rule names; the latter are
evaluated in the context of the rule definition.

###### Example

For generating language bindings for protocol buffers, a
rule might look as follows.

``` jsonc
{ "cc_proto_bindings":
  { "target_fields": ["proto_deps"]
  , "anonymous":
    { "protos":
      { "target": "proto_deps"
      , "provider": "proto"
      , "rule_map": {"proto_library": "cc_proto_library"}
      }
    }
  , "expression": {...}
  }
}
```

##### Evaluation mechanism

The evaluation of a target defined by a user-defined rule is
handled as follows. After the target fields are evaluated as
usual, an additional step is carried out.

For each anonymous-target field, i.e., for each key in the
`"anonymous"` map, a list of anonymous targets is generated from
the corresponding value: take all targets from the specified
`"target"` field in all their specified configuration
transitions (they have already been evaluated) and take the
values provided for the specified `"provider"` key (using the
empty list as default). That value has to be a list of nodes.
All the node lists obtained that way are concatenated. The
configuration transition for the respective field name is
evaluated. Those targets are then evaluated for all the
transitioned configurations requested.

In the final evaluation of the defining expression, the
anonymous-target fields are available in the same way as any
other target field. Also, they contribute to the effective
configuration in the same way as regular target fields.
