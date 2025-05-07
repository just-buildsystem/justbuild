Dependency management: `just serve`
===================================

`just serve` starts a remote target-level cache service (for ease called from
now on the *serve service*) that can maintain and provide project dependencies
via an associated remote-execution endpoint. The serve service is also
responsible, in the case of a target-level cache miss when performing the target
lookup, to build the relevant target (by dispatching it to a remote execution
endpoint) and thus remember the result in its cache for subsequent requests.
It has to be mentioned that we are only interested here in `export` targets in
content-fixed repositories, as those are the ones eligible to be cached in the
first place.

The usefulness of such a service are several

- projects can have many dependencies. These tend to not be part of the build
  environment and are instead built from source, but with *justbuild* these
  result in content-fixed repositories (including logically, via the `"to_git"`
  pragma), increasing reproducibility and easing target caching.

- projects tend to share dependencies. By connecting to the same target-level
  cache service, and as dependencies tend to be updated less frequently than the
  code being actively developed, projects can expect frequent target cache hits,
  reducing overall build times.

- easier maintenance of necessary archives, promoting reproducibility and
  improving auditing.

- projects can avoid having to fetch their dependencies locally, thus reducing
  the amount of data needed to be present for a successful build.

The way this service interacts with the other *justbuild* components is
described in the following diagram:

                                  ┌───────────┐
             ┌───────────────────>│ execution │
             │  ┌─────────────────┤ endpoint  │
             │  │                 └──────┬────┘
       build │  │ artifacts            ^ │
    dispatch │  │                      │ │
             │  v                      │ │
         ┌───┴───────┐                 │ │
         │  serve    │                 │ │
         │  endpoint │                 │ │
         └───────────┘                 │ │
               ^                       │ │
               │                       │ │
               │                       │ │
    ...........│.......................│.│..........
               │                       │ │
               │                       │ │
         basic │                 build │ │ artifacts
          RPCs │              dispatch │ │
               │                       │ │
               │       ┌────────┐      │ │
               │       │        ├──────┘ │
               └──────>│ client │        │
                       │        │<───────┘
                       └────────┘

As can be seen, the serve endpoint doubles as another client of the 
remote-execution endpoint, able to dispatch builds for export targets it has no
cache entry for and retrieve the resulting build artifacts. In fact, any
CAS object that should normally need to be transacted between the client and the
serve endpoint happens only via the remote-execution endpoint, with direct
communication being restricted to basic RPCs (which have minimal information and
are thus very small). This reduces the communication taking place between the
client and the serve endpoint, while ensuring that the remote CAS has all the
opportunities to cache entries between builds. This comes at a slight cost of
extra communication between the serve and execution endpoints (to sync various
requested entries), however in a typical deployment these endpoints are 
expected to be close network-wise.

In the remainder of this tutorial section, we will first cover some basics of
setting up the serve service, then we will showcase how to set up and use
`just serve` to provide first- and third-party dependencies to a *justbuild*
project by expanding on a previously covered example scenario.

Basic usage of `just serve`
---------------------------

To simplify usage, `just serve` supports only one argument, the path to a
configuration file containing all the options needed to set up the service.
Therefore the serve service can simply be started by typing in a shell

```sh
$ just serve <CONFIG_FILE>
```

The format of the configuration file is `JSON`. For the purposes of this
tutorial section, we will consider from now on as our configuration file the
path `.just-servec` in the current working directory.

Let us see what calling `just serve` with an empty configuration file does and
interpret the result. Thus, using a configuration file with content

``` {.jsonc srcname=".just-servec"}
{}
```

results in

``` sh
$ just serve .just-servec
INFO: serve and execute services started: {"interface":"127.0.0.1","pid":3728424,"port":34429}

```

First of all, the message mentions two services: `serve` and `execute`. As
a reminder from the tutorial section on *Building Third-party Software*,
running `just execute` starts a single-node remote build execution service in
the current environment. Our `just serve` dependency management service always
requires an associated remote-execution endpoint to be provided in order for
various artifacts or Git objects to be transacted with a client via the CAS of
the remote-execution endpoint. Therefore, calling `just serve` without
specifying an existing remote-execution endpoint automatically creates for us a
single-node remote-execution service, exposed at the same socket address.

Once the serve service is started, it logs out three pieces of data:
- which interface is used. In this case, the default one, which is the
  loopback device.
- the pid number. This changes with each invocation.
- the used port. In this case, the default one, a random free port was
  automatically chosen.

Now the serve service can be used by running, in a different shell

``` sh
$ just [...] -R localhost:34429
# or
$ just-mr -R localhost:34429 [...]
```

Note that if we do not specify explicitly the remote-execution endpoint (via
option `-r`), the tool will assume a remote-execution endpoint is exposed on
the same socket (HOST:PORT pair). A mismatch in the remote-execution endpoint
specified by the client and the one set up for the `just serve` instance will
result in an error.

### Explicit interface, port or existing remote-execution endpoint

There are good situations where relying on defaults is preferred. For example
- the loopback device is a good option when the serve service runs on a
dedicated machine;
- relying on the default single-node remote-execution service means one can
quickly start using the service on a shared machine with minimal effort, e.g.,
in a small team or for fast prototyping;
- sometimes one does not know a fixed port number available for the desired
serve endpoint.

However, in most cases a remote-execution endpoint already exists and a finer
control on the service endpoint is desired. The `just serve` configuration file
can easily be set up with such information. For example, a serve service
configured with

``` {.jsonc srcname=".just-servec"}
{ "remote service":
  { "interface": "127.0.0.1"
  , "port": 9999
  }
, "execution endpoint": {"address": "127.0.0.1:8989"}
}
```

could then be exploited in a different shell as

``` sh
$ just [...] -R 127.0.0.1:9999 -r 127.0.0.1:8989
# or
$ just-mr -R 127.0.0.1:9999 -r 127.0.0.1:8989 [...]
```

In general, we recommend that the serve endpoint is _close_ to its associated
remote-execution endpoint in a network sense (same subnet, or even same host, as
above). More importantly, as with any service over the network, security should
be considered, therefore next we will cover server and client certification,
which ensure both the needed security, but also allows client access management.

### Enable mTLS

As with `just execute`, mTLS must be enabled when the service starts, and it
cannot be activated (or deactivated) while the serve service is running.

The configuration file can be extended to specify both the client and server
certification, such as

``` {.jsonc srcname=".just-servec"}
...
, "remote-service":
  ...
  , "server cert":
    {"root": "system", "path": "etc/just-serve/certs/server.crt"}
  , "server key": 
    {"root": "system", "path": "etc/just-serve/certs/server.key"}
  ...
, "authentication":
  { "ca cert": {"root": "system", "path": "etc/just-serve/certs/ca.crt"}
  , "client cert": 
    {"root": "system", "path": "etc/just-serve/certs/client.crt"}
  , "client key": 
    {"root": "system", "path": "etc/just-serve/certs/client.key"}
  }
...
```

It must be noted that we expect the same `CA certificate` was used on the
associated remote-execution endpoint, and naturally any client of the serve
service should connect by passing the same `CA certificate` and a pair of
certificate and private key signed by the same authority as the serve and
execution endpoints. 

Therefore, a client using the serve service will connect as

``` sh
$ just [...] -R <serve_endpoint> [-r <execution_endpoint>] --tls-ca-cert <path_to_CA_cert> \
             --tls-client-cert <path_to_client_cert> --tls-client-key <path_to_client_key>
# or
$ just-mr -R <serve_endpoint> [-r <execution_endpoint>] --tls-ca-cert <path_to_CA_cert> \
          --tls-client-cert <path_to_client_cert> --tls-client-key <path_to_client_key> [...]
```

Note that the serve configuration file requires location objects to be
specified (which can be relative to the system root or to the current user's
home directory), while the command line argument paths are expected to be
either relative to the invocation directory or absolute.

### Known repositories

A simple way to maintain the dependencies of a project is to store its list of
needed third-party archives under version control (for our tool, a Git
repository). For this purpose, `just serve` can be configured to be made aware
at startup of local (with respect to the service environment) Git repositories
checkout locations, places where the service can look for required Git objects,
such as archives or repository roots. To do this, the configuration file can be
extended, for example, with

``` {.jsonc srcname=".just-servec"}
...
, "repositories":
  [ {"root": "system", "path": "var/repos/third-party-distfiles"}
  , {"root": "system", "path": "var/repos/project-foo"}
  , {"root": "system", "path": "var/repos/project-bar"}
  ]
...
```

### Local build root

In order to provide values of target-cache entries to the client, the serve
service has to have its own target-level cache, and thus its own 
_local build root_. If one does not want to use the usual default location, the
configuration file can be extended, for example, with

``` {.jsonc srcname=".just-servec"}
...
, "local build root": {"root": "system", "path": "var/cache/serve-build-root"}
...
```

Do keep in mind that the build root must be given as an absolute path.

Importantly, the local build root, as is generally the case for *justbuild*, is
the only place where the tool will write.

### Info file

To more easily handle the logged data provided by the running service, i.e.,
interface, pid, and port, we can configure the serve service to provide this
information also in a file, which then can simply be parsed. To do this we can
extend the `"remote service"` field accordingly

``` {.jsonc srcname=".just-servec"}
...
, "remote-service":
  ...
  , "pid file": {"root": "system", "path": "var/run/info.json"}
  ...
...
```

Serving export targets
----------------------

For the following, we return to the *hello_world* example from the section on 
[*Building Third-party dependencies*](./third-party-software.md), which depends
on the open-source project [fmtlib](https://github.com/fmtlib/fmt).
Our `repos.json` at this stage reads:

``` {.jsonc srcname="repos.json"}
{ "main": "tutorial"
, "repositories":
  { "rules-cc":
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "7a2fb9f639a61cf7b7d7e45c7c4cea845e7528c6"
      , "repository": "https://github.com/just-buildsystem/rules-cc.git"
      , "subdir": "rules"
      }
    , "target_root": "tutorial-defaults"
    , "rule_root": "rules-cc"
    }
  , "tutorial":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"rules": "rules-cc", "format": "fmtlib"}
    }
  , "tutorial-defaults":
    { "repository":
      { "type": "file"
      , "path": "./tutorial-defaults"
      , "pragma": {"to_git": true}
      }
    }
  , "fmt-targets-layer":
    { "repository":
      { "type": "file"
      , "path": "./fmt-layer"
      , "pragma": {"to_git": true}
      }
    }
  , "fmtlib":
    { "repository":
      { "type": "git"
      , "branch": "8.1.1"
      , "commit": "b6f4ceaed0a0a24ccf575fab6c56dd50ccf6f1a9"
      , "repository": "https://github.com/fmtlib/fmt.git"
      }
    , "target_root": "fmt-targets-layer"
    , "bindings": {"rules": "rules-cc"}
    }
  }
}
```

What we would like to do is use a `just serve` instance to provide the `"fmt"`
export target, used to build the *hello_world* binary. In our final setup, the
source code of the `fmtlib` library would never need to ever be fetched by any
client building this project.

To achieve this, in simple terms, the requirements needed to be met to build
the `"fmt"` export target locally (as done so far) will need to be met by the
serve endpoint. The following diagram showcases the distribution of roots
between the client and the serve endpoint

                Locally                  .             On the serve endpoint
                                         .
                                         .             workspace_root  ┌──────────┐
                                         .            ┌───────────────>│ "fmtlib" │
    ┌─────────────────────┐  target_root . ┌───────┐  │                └──────────┘
    │ "fmt-targets-layer" │<───────────────┤ "fmt" ├──┤
    └─────────────────────┘              . └───────┘  │ bindings       ┌────────────┐
                                         .            └───────────────>│ "rules-cc" │
                                         .                             └────────────┘

What this diagram showcases is that for the `"fmt"` target
- the source code is provided by repository `"fmtlib"`. This needs to be made
  available to the serve endpoint and a correct setup (as will be shown) can
  ensure that the client never has to fetch this repository locally.
- the target description is provided by repository `"fmt-targets-layer"`, made
  content-defined by the `"to_git"` pragma and available locally in the Git
  checkout `./tutorial-defaults`. What makes `just serve` more than a simple
  dependency manager is that it allows for the target description to be changed
  locally and dispatch a build of a target without ever having to have the
  source code.
- the repository `"rules-cc"` is needed on the serve endpoint as a transitive
  dependency, as it contains the rules by which the `"fmt"` target needs to be
  built.
- finally, the build of target `"fmt"` is left to the serve endpoint, which will
  dispatch it to the respective remote-execution endpoint, with the client just
  receiving the target-level cache value and resulting artifacts.

In the following we will showcase how a `just serve` instance can be configured
for this scenario, together with the changes needed in the local `repos.json`
build description in order to build a *hello_world* binary from a client that
never has to fetch the source code of fmtlib.

Let us start with a basic `.just-servec` configuration file, on which we can
then expand step-by-step

``` {.jsonc srcname=".just-servec"}
{ "local build root": {"root": "system", "path": "var/cache/serve-build-root"}
, "remote service": {"port": 9999}
}
```

### Serving third-party dependencies: "fmtlib"

For building third-party dependencies from source the typical input is in the
form of archived packages. This ensures easy auditing, code reproducibility,
and long term offline availability for analysis and, of course, building.

The `"fmtlib"` repository falls under this category, therefore in order to make
it easily maintainable via a serve endpoint we need to switch its description
to an archive-type repository. The fmtlib project offers for the tagged commit
used by us so far an
[official packaged version](https://github.com/fmtlib/fmt/releases/download/8.1.1/fmt-8.1.1.zip)
(Git object hash: fd4144c2835f89516cac0db1f3c7b73562555dca), therefore the first
step is to change repository `"fmtlib"` in our `repos.json` to type `"zip"`,
populating its required fields as needed.

Secondly, we can instruct *justbuild* to treat this repository as _absent_,
which means that `just-mr` will set up this repository root such that it is not
held locally, but it is known to the serve endpoint. This does not by itself
exclude the possibility that the fmtlib archive will never be fetched, but, as
will be shown below, pairing it with a good setup of the serve server will most
certainly achieve this.

With all these changes, the `"fmtlib"` entry in our `repos.json` now reads

``` {.jsonc srcname="repos.json"}
...
  , "fmtlib":
    { "repository":
      { "type": "zip"
      , "content": "fd4144c2835f89516cac0db1f3c7b73562555dca"
      , "fetch": "https://github.com/fmtlib/fmt/releases/download/8.1.1/fmt-8.1.1.zip"
      , "subdir": "fmt-8.1.1"
      , "pragma": {"absent": true}
      }
    , "target_root": "fmt-targets-layer"
    , "bindings": {"rules": "rules-cc"}
    }
...
```

Now let us discuss what preparations are needed for the serve service in order
to avoid clients having to fetch archives from the network.

The typical way a project keeps track of their third-party packages is under
some version control, e.g., a Git repository. Assuming the referenced fmtlib
archive is available in such a Git repository, we can create a local checkout
at path `/var/repos/distfiles`. Then the following command should pass

``` sh
$ git -C /var/repos/distfiles cat-file -t fd4144c2835f89516cac0db1f3c7b73562555dca
blob
$
```

This repository checkout can then be made available to the serve service by
adding its path to the known repository list in the `.just-servec` configuration
file, which will become

``` {.jsonc srcname=".just-servec"}
{ "local build root": {"root": "system", "path": "var/cache/serve-build-root"}
, "remote service": {"port": 9999}
, "repositories": [{"root": "system", "path": "var/repos/distfiles"}]
}
```

Do keep in mind that deciding which Git checkout locations are known to the
serve endpoint needs to be decided before the service is started, and no
modification to its configuration can be done during its operation. Any updates
would thus need a redeployment of the service.

### Serving first-party dependencies: "rules-cc"

Projects in general can also depend on first-party code, which is more actively
developed, such as code developed in the same organization by other teams, that
is more closely coupled and more frequently updated than third-party code. For
the most case, projects will want to use up-to-date verified versions of these
codes, for example the latest commit of a Git repository which uses proper CI
integration to test everything merged to its main branch.

In our tutorial example, we will consider `"rules-cc"` as a first-party
dependency. We will start with the serve service setup.

As we want to be able to easily change the commit we are interested in whenever
we expect the latest version of this repository in our project, on the serve
server we will create a local Git checkout of `"rules-cc"` stored at 
`/var/repos/rules-cc`. This means that a client can easily update its build
description to point to whichever commit it needs, while the serve server will
have to ensure this checkout is always kept updated (usually automatized, for
example via a cron job). The configuration file of the serve service will need
thus to be updated to include this checkout location, so `.just-servec` now
reads

``` {.jsonc srcname=".just-servec"}
{ "local build root": {"root": "system", "path": "var/cache/serve-build-root"}
, "remote service": {"port": 9999}
, "repositories": 
  [ {"root": "system", "path": "var/repos/distfiles"}
  , {"root": "system", "path": "var/repos/rules-cc"}
  ]
}
```

On the client side for our *hello_world* example we however cannot mark the
`"rules-cc"` as absent. This is because while the `"fmt"` export target
requiring this binding can be served, the main target of the tutorial is fully
local, which requires `"rules-cc"` to be present. However, the information on
which rule root is used is known, so during the dispatched build the serve
endpoint will search for this root in its known repositories and find it in
the prepared local checkout.

### Putting it all together

We are now ready to see how this setup works. At this point the `repos.json` is

``` {.jsonc srcname="repos.json"}
{ "main": "tutorial"
, "repositories":
  { "rules-cc":
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "7a2fb9f639a61cf7b7d7e45c7c4cea845e7528c6"
      , "repository": "https://github.com/just-buildsystem/rules-cc.git"
      , "subdir": "rules"
      }
    , "target_root": "tutorial-defaults"
    , "rule_root": "rules-cc"
    }
  , "tutorial":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"rules": "rules-cc", "format": "fmtlib"}
    }
  , "tutorial-defaults":
    { "repository":
      { "type": "file"
      , "path": "./tutorial-defaults"
      , "pragma": {"to_git": true}
      }
    }
  , "fmt-targets-layer":
    { "repository":
      { "type": "file"
      , "path": "./fmt-layer"
      , "pragma": {"to_git": true}
      }
    }
  , "fmtlib":
    { "repository":
      { "type": "zip"
      , "content": "fd4144c2835f89516cac0db1f3c7b73562555dca"
      , "fetch": "https://github.com/fmtlib/fmt/releases/download/8.1.1/fmt-8.1.1.zip"
      , "subdir": "fmt-8.1.1"
      , "pragma": {"absent": true}
      }
    , "target_root": "fmt-targets-layer"
    , "bindings": {"rules": "rules-cc"}
    }
  }
}
```

and the `.just-servec` configuration file is

``` {.jsonc srcname=".just-servec"}
{ "local build root": {"root": "system", "path": "var/cache/serve-build-root"}
, "remote service": {"port": 9999}
, "repositories": 
  [ {"root": "system", "path": "var/repos/distfiles"}
  , {"root": "system", "path": "var/repos/rules-cc"}
  ]
}
```

We can now start the serve service

``` sh
$ just serve .just-servec
INFO: serve and execute services started: {"interface":"127.0.0.1","pid":4178555,"port":9999}

```

and, in a different shell, build *hello_world* in a clean build root using this
serve endpoint

``` sh
$ just-mr -R localhost:9999 --local-build-root ~/local-build-root build helloworld
INFO: Performing repositories setup
INFO: Found 5 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","--local-build-root","/home/tutorial/local-build-root","-R","127.0.0.1:9999","helloworld"]
INFO: Using '127.0.0.1:9999' as the remote execution endpoint.
INFO: Requested target is [["@","tutorial","","helloworld"],{}]
INFO: Analysed target [["@","tutorial","","helloworld"],{}]
INFO: Export targets found: 0 cached, 1 served, 0 uncached, 0 not eligible for caching
INFO: Discovered 4 actions, 2 trees, 0 blobs
INFO: Building [["@","tutorial","","helloworld"],{}].
INFO: Processed 4 actions, 0 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [18d25e828a0176cef6fb029bfd83e1862712ec87:132736:x]
$
```

We use a clean build root (here, in the `$HOME` directory of the current user,
in our example `/home/tutorial/`) to show that we indeed not use any old cache
entries from previous builds and that everything marked absent really comes
from the serve endpoint. The running shell where the serve service was started
in will have been updated to

``` log
INFO: serve and execute services started: {"interface":"127.0.0.1","pid":4178555,"port":9999}
INFO (target-service): Analysed target [["@","0","","fmt"],{"ADD_CXXFLAGS":null,"AR":null,"CXX":null,"CXXFLAGS":null,"ENV":null}]
INFO (execution-service): Execute 62a33fc12031c240d38d12b183a47f79e9ce90ea58
INFO (execution-service): Execute 62af85caddbbc28caa08d98e004c6fa8772f69b057
INFO (execution-service): Execute 6233fc78dcdcde8fd3c4bdda7cbaf7f915d8c7a01b
INFO (execution-service): Execute 62d181d81cac1e6a8c331c3eac643fb9ad0cac4cdd

```

showing that the `"fmt"` export target has been analysed and built by the serve
endpoint (with the build dispatched to the associated remote-execution
endpoint, which in our case coincides with the serve endpoint, hence the
merged output information).

### Final notes

For our example project we, of course, worked locally and deployed the serve
endpoint on the loopback device. Those who want to further test this
*hello_world* with a serve endpoint deployed actually on, e.g., a different
physical or logical partition, to better simulate the client-server separation,
would have to move to the new server deployment location just the `.just-servec`
configuration file and the `repos` directory.

Absent repositories and rc-files
----------------------------------

As covered in the above, once we have locally a correct build description
separating first- and third-party dependencies, the only change made locally
to request that repository roots be served by a serve endpoint is the addition
of the pragma tag `{"absent": true}`. While in our example we only dealt with
one absent repository as we had one export target, projects usually have tens
of dependencies providing tens or hundreds of export targets, all of which could
be managed and served by a suitably set up `just serve` instance. With different
clients having different local and remote setups, it is preferred if one main
build description is packaged with a project and its interaction with the
various remote endpoints (serve and execution) can be easily maintained by the
user.

Thankfully, `just-mr` accepts as an argument a _repository configuration_ file
(or _rc-file_, for short), which can hold not only all the usual command line
arguments, but also extra arguments, such as one related to absent repositories.

Let us show how this work. First, as stated, we clean up our `repos.json` by
removing any `"absent"` pragma fields

``` {.jsonc srcname="repos.json"}
{ "main": "tutorial"
, "repositories":
  { "rules-cc":
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "7a2fb9f639a61cf7b7d7e45c7c4cea845e7528c6"
      , "repository": "https://github.com/just-buildsystem/rules-cc.git"
      , "subdir": "rules"
      }
    , "target_root": "tutorial-defaults"
    , "rule_root": "rules-cc"
    }
  , "tutorial":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"rules": "rules-cc", "format": "fmtlib"}
    }
  , "tutorial-defaults":
    { "repository":
      { "type": "file"
      , "path": "./tutorial-defaults"
      , "pragma": {"to_git": true}
      }
    }
  , "fmt-targets-layer":
    { "repository":
      { "type": "file"
      , "path": "./fmt-layer"
      , "pragma": {"to_git": true}
      }
    }
  , "fmtlib":
    { "repository":
      { "type": "zip"
      , "content": "fd4144c2835f89516cac0db1f3c7b73562555dca"
      , "fetch": "https://github.com/fmtlib/fmt/releases/download/8.1.1/fmt-8.1.1.zip"
      , "subdir": "fmt-8.1.1"
      }
    , "target_root": "fmt-targets-layer"
    , "bindings": {"rules": "rules-cc"}
    }
  }
}
```

One can easily check that this allows us to build locally again, with the
note that, as we have changed the `"fmtlib"` repository type, we need a fetch
of the archive. But, of course, we do ***not*** want to fetch this archive and
build locally, just maintain a build description that ***could*** do so in the
absence of a properly set up serve endpoint.

Next we create a file `absent.json` with content

``` {.jsonc srcname="absent.json"}
["fmtlib"]
```

This has a single `JSON` list of the names of the repositories that should be
marked as absent. We have chosen a descriptive file name, but one is free to
choose another.

Then we need to set up the _rc-file_ of `just-mr`. By default, `just-mr` always
tries to read such a configuration file, with the default location being
`$HOME/.just-mrrc`. Here we will pass this file explicitly, so we create file
`rc-file` in the current directory with content

``` {.jsonc srcname="rc-file"}
{ "local build root": {"root": "home", "path": "local-build-root"}
, "remote serve": {"address": "localhost:9999"}
, "absent": [{"root": "workspace", "path": "absent.json"}]
}
```

Let us break it down. The local build root is given using a construct we call
a _location object_, specifying a path relative to some root type. In this case,
we use root `"home"` to signal that this path is relative to the current user's
`${HOME}` directory (e.g., in the example below `/home/tutorial/`).
Next we specify the serve endpoint address, which is straight-forward. Lastly,
we specify the list of repositories to be marked absent by giving the
`absent.json` path relative to the workspace. This is because typically one
would keep such a file in the project's tree, as it contains information
pertinent to this particular project only. The presence of the `ROOT` file in
the current directory ensures the file will be picked up by `just-mr`.

With all this, we can rebuild, using this _rc-file_ and with the same serve
endpoint still running, successfully

``` sh
$ just-mr --rc rc-file build helloworld
INFO: Performing repositories setup
INFO: Found 5 repositories to set up
INFO: Setup finished, exec ["just","build","-C","...","--local-build-root","/home/tutorial/local-build-root","-R","localhost:9999","helloworld"]
INFO: Using 'localhost:9999' as the remote execution endpoint.
INFO: Requested target is [["@","tutorial","","helloworld"],{}]
INFO: Analysed target [["@","tutorial","","helloworld"],{}]
INFO: Export targets found: 0 cached, 1 served, 0 uncached, 0 not eligible for caching
INFO: Discovered 4 actions, 2 trees, 0 blobs
INFO: Building [["@","tutorial","","helloworld"],{}].
INFO: Processed 4 actions, 4 cache hits.
INFO: Artifacts built, logical paths are:
        helloworld [18d25e828a0176cef6fb029bfd83e1862712ec87:132736:x]
$
```

Lastly, it should be mentioned that the `just-mr` rc-file can be instructed to
import configurations from other rc-files, overwriting (or expanding, for
list-type fields) the values in the main one. This means that a minimal desired
configuration can be shipped with the project, and users then can import it
into their local rc-file (which possibly gets shared by several projects). If
we assume file `shipped-rc-file` contains

``` {.jsonc srcname="rc-file"}
{ "remote serve": {"address": "127.0.0.1:9900"}
, "remote execution": {"address": "127.0.0.1:8989"}
}
```

then using a modified `rc-file` reading

``` {.jsonc srcname="rc-file"}
{ "local build root": {"root": "home", "path": "local-build-root"}
, "remote serve": {"address": "localhost:9999"}
, "absent": [{"root": "workspace", "path": "absent.json"}]
, "rc files": [{"root": "workspace", "path": "shipped-rc-file"}]
}
```

would be equivalent to the single combined configuration of

``` {.jsonc}
{ "local build root": {"root": "home", "path": "local-build-root"}
, "remote serve": {"address": "127.0.0.1:9900"}
, "absent": [{"root": "workspace", "path": "absent.json"}]
, "remote execution": {"address": "127.0.0.1:8989"}
}
```
