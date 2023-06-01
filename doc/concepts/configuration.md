Configuration
=============

Targets describe abstract concepts like "library". Depending on
requirements, a library might manifest itself in different ways. For
example,

 - it can be built for various target architectures,
 - it can have the requirement to produce position-independent code,
 - it can be a special build for debugging, profiling, etc.

So, a target (like a library described by header files, source files,
dependencies, etc) has some additional input. As those inputs are
typically of a global nature (e.g., a profiling build usually wants all
involved libraries to be built for profiling), this additional input,
called "configuration" follows the same approach as the `UNIX`
environment: it is a global collection of key-value pairs and every
target picks, what it needs.

Top-level configuration
-----------------------

The configuration is a `JSON` object. The configuration for the target
requested can be specified on the command line using the `-c` option;
its argument is a file name and that file is supposed to contain the
`JSON` object.

Propagation
-----------

Rules and target definitions have to declare which parts of the
configuration they want to have access to. The (essentially) full
configuration, however, is passed on to the dependencies; in this way, a
target not using a part of the configuration can still depend on it, if
one of its dependencies does.

### Rules configuration and configuration transitions

As part of the definition of a rule, it specifies a set `"config_vars"`
of variables. During the evaluation of the rule, the configuration
restricted to those variables (variables unset in the original
configuration are set to `null`) is used as environment.

Additionally, the rule can request that certain targets be evaluated in
a modified configuration by specifying `"config_transitions"`
accordingly. Typically, this is done when a tool is required during the
build; then this tool has to be built for the architecture on which the
build is carried out and not the target architecture. Those tools often
are `"implicit"` dependencies, i.e., dependencies that every target
defined by that rule has, without the need to specify it in the target
definition.

### Target configuration

Additionally (and independently of the configuration-dependency of the
rule), the target definition itself can depend on the configuration.
This can happen, if a debug version of a library has additional
dependencies (e.g., for structured debug logs).

If such a configuration-dependency is needed, the reserved key word
`"arguments_config"` is used to specify a set of variables (if unset,
the empty set is assumed; this should be the usual case). The
environment in which all arguments of the target definition are
evaluated is the configuration restricted to those variables (again,
with values unset in the original configuration set to `null`).

For example, a library where the debug version has an additional
dependency could look as follows.

``` jsonc
{ "libfoo":
  { "type": ["@", "rules", "CC", "library"]
  , "arguments_config": ["DEBUG"]
  , "name": ["foo"]
  , "hdrs": ["foo.hpp"]
  , "srcs": ["foo.cpp"]
  , "local defines":
    { "type": "if"
    , "cond": {"type": "var", "name": "DEBUG"}
    , "then": ["DEBUG"]
    }
  , "deps":
    { "type": "++"
    , "$1":
      [ ["libbar", "libbaz"]
      , { "type": "if"
        , "cond": {"type": "var", "name": "DEBUG"}
        , "then": ["libdebuglog"]
        }
      ]
    }
  }
}
```

Effective configuration
-----------------------

A target is influenced by the configuration through

 - the configuration dependency of target definition, as specified in
   `"arguments_config"`,
 - the configuration dependency of the underlying rule, as specified in
   the rule's `"config_vars"` field, and
 - the configuration dependency of target dependencies, not taking into
   account values explicitly set by a configuration transition.

Restricting the configuration to this collection of variables yields the
effective configuration for that target-configuration pair. The
`--dump-targets` option of the `analyse` subcommand allows to inspect
the effective configurations of all involved targets. Due to
configuration transitions, a target can be analyzed in more than one
configuration, e.g., if a library is used both, for a tool needed during
the build, as well as for the final binary cross-compiled for a
different target architecture.
