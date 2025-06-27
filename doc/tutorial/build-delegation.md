# More build delegation using a serve endpoint

The original purpose of a `just serve` endpoint is to allow building
against dependencies without having to download them. That is
particularly important when [bootstrapping](https://bootstrappable.org/)
the toolchain. However, the serve endpoint does not care what the
target actually is. As long as it is a content-fixed `export` target,
it has all the necessary roots. Therefore, it can also be used for
other purposes.

## Example: Analysing large data sets

Besides sources of long bootstrap chains, all form of measurement
data are also files that one wants to avoid having to download,
while still analysing them in various ways and by several persons.

### Making data available to serve

Depending on the nature of the data set to be analysed, several ways
are appropriate to make it available to serve. Data for long-term
archival, such as experimental measurements, can be committed to a
repository and that repository added to the `"repositories"` field
in the serve configuration as usual.

There is, however, another possibility more suited for data
to be rotated, like monitoring data or invocation-log data written
by `just-mr`. Each entity generating such data (like monitoring
machine, CI runner, etc.) uploads the data directory to the
remote-execution endpoint, e.g., via `just-mr add-to-cas` and only
distributes the tree hash to the entities analysing the data.

As a user of the serve endpoint, by just knowing the tree hash,
can construct an absent root from it.

``` jsonc
{ "repository":
      { "type": "git tree"
      , "id": "..."
      , "cmd": ["false", "Should be known to CAS"]
      , "pragma": {"absent": true}
      }
    }
```

Of course, the command `false` is not able to create the specified
tree, but it should not be executed anyway, especially as we don't
want to ever have that large tree locally. Buildings against this
root still makes it available to serve without ever fetching it; the
reason this works is that `just-mr` always prefers the network-wise
closest path: if the root is not known to the serve endpoint anyway,
but is known to the remote-execution CAS, it simply asks serve
to fetch it from there. No need to get the root local, as it is
marked absent.

Of course, the above root description is so systematic, that we
can easily generate it from the hash; this is useful if we have
many data sets uploaded individually and hence need many of those
repositories.

### Analysing data via serve

To analyse a data set, we need, besides the actual data, also a
target description and, potentially, additional tools. Here we use
that `just` allows separate layers for sources and targets. So we
can add a separate repository with the targets file for analysing
the data. As that one will typically be small, we can write it
locally (allowing us the experiment with different kinds of statistics
we might care about) and mark it as `"to_git"`. This not only
makes it content-fixed, but also ensures that it will be uploaded
to the serve endpoint. For computations delegated to serve, we can
only access export targets; but while measurement data might have
some random component, analysing that data typically is a pure
function. So a simple target file could look as follows.

``` jsonc
{ "": {"type": "export", "target": "stats"}
, "stats":
  { "type": "generic"
  , "outs": ["stats.json"]
  , "cmds": ["./statistics-tool"]
  , "deps": ["data", ["@", "tools", "", "statistics-tool"]]
  }
, "data": {"type": "install", "dirs": [[["TREE", null, "."], "data"]]}
}
```

If the data tree contains several data sets that can be analysed independently,
instead of using a big action, several tasks can be defined using computed
roots. If many different data trees are uploaded, an overall accumulation of
the data of the individual repositories can be carried out.
