Action-controlled execution properties
======================================

Motivation
----------

### Varying execution platforms

It is a common situation that software is developed for one platform,
but it is desirable to build on a different one. For example, the other
platform could be faster (common theme when developing for embedded
devices), cheaper, or simply available in larger quantities. The
standard solution for these kind of situations is cross compiling: the
binary is completely built on one platform, while being intended to run
on a different one. This can be achieved by constructing the compiler
invocations accordingly and is already built into our rules (at least
for `C` and `C++`).

The situation changes, however, once testing (especially end-to-end
testing) comes into play. Here, we actually have to run the built
binary---and do so on the target architecture. Nevertheless, we still
want to offload as much as possible of the work to the other platform
and perform only the actual test execution on the target platform. This
requires a single build executing actions on two (or more) platforms.

### Varying execution times

#### Calls to foreign build systems

Often, third-party dependencies that natively build with a different
build system and don't change too often (yet often enough to not
have them part of the build image) are simply put in a single
action, so that they get built only once, and then stay in cache for
everyone. This is precisely, what our `rules-cc` rules like
`["CC/foreign/make", "library"]` and `["CC/foreign/cmake", "library"]` do.

For those compound actions, we of course expect them to run longer
than normal actions that only consist of a single compiler or linker
invocation. Giving an absolute amount of time needed for such an
action is not reasonable, as that very much depends on the
underlying hardware. However, it is reasonable to give a number
"typical" actions this compound action corresponds to.

#### Long-running end-to-end tests

A similar situation where a significantly longer action is needed in
a build otherwise consisting of short actions are end-to-end tests.
Test using the final binary might have a complex set up, potentially
involving several instances running to test communication, and
require a lengthy sequence of interactions to get into the situation
that is to be tested, or to verify the absence of degrading of the
service under high load or extended usage.

Interfaces related to action-controlled execution properties
------------------------------------------------------------

### Properties of the `"ACTION"` function

The `"ACTION"` function available in the rule definition also has
the following attributes. All of those attributes are optional.

#### `"execution properties"`

This value has to evaluate to a map of strings or `null`;
if not given or evaluating to `null`, the
empty map is taken as default. This map is taken as a union with any
remote-execution properties specified at the invocation of the build
(if keys are defined both, for the entire build and in
`"execution properties"` of a specific action, the latter takes
precedence).

Local execution continues to any execution properties specified.
However, with the dispatch functionality of `just` described later, such
execution properties can also influence a build that is local by
default.

#### `"timeout scaling"`

If given, the value has to evaluate to a number greater or equal than
`1.0`, or `null`. If not given, or evaluating to `null`, the value
`1.0` is taken as default. The action timeout specified for this
build (the default value, or whatever is specified on the command
line) is multiplied by the given factor and taken as timeout for
this action. This applies for both, local and remote builds.

### Execution properties of the the built-in `"generic"` rule

As the built-in `"generic"` rule basically is there to allow the
definition of an action in an ad-hoc fashion, it also provides the
same attributes. More precisely, the fields `"timeout scaling"`
and `"execution properties"` are taken as additional arguments to
the underlying action, with the same semantics as the respective
fields of the `"ACTION"` constructor.

### `just` dispatching based on remote-execution properties

In simple setups, like using `just execute`, the remote execution is not
capable of dispatching to different workers based on remote-execution
properties. To nevertheless have the benefits of using different
execution environments, `just` allows an optional configuration file
to be passed on the command line via a new option
`--endpoint-configuration`. This configuration file contains a list
of pairs of remote-execution properties and remote-execution endpoints.
The first matching entry (i.e., the first entry where the
remote-execution property map coincides with the given map when
restricted to its domain) determines the remote-execution endpoint to be
used; if no entry matches, the default remote-execution endpoint is
used. In any case, the remote-execution properties are forwarded to the
chosen remote-execution endpoint without modification.

When connecting a non-standard remote-execution endpoint, `just` will
ensure that the applicable CAS of that endpoint will have all the needed
artifacts for that action. It will also transfer all result artifacts
back to the CAS of the default remote-execution endpoint.
