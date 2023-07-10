Target-level caching as a service
=================================

Motivation
----------

Projects can have quite a lot of dependencies that are not part of the
build environment, but are, instead, built from source, e.g., in order
to always build against the latest snapshot. The latter is a typical
workflow in case of first-party dependencies. In the case of
`justbuild`, those first-party dependencies form a separate logical
repository that is typically content fixed (e.g., because that
dependency is versioned in a `git` repository).

Moreover, code is typically first built (and tested) by the owning
project before being used as a dependency. Therefore, if remote
execution is used, for a first-party dependency, we expect all actions
to be in cache. As dependencies are typically updated less often than
the code being developed is changed, in most builds, the dependencies
are in target-level cache. In other words, in a remote-execution setup,
the whole code of dependencies is fetched just to walk through the
action graph a single time to get the necessary cache hits.

Proposal: target-level caching as a service
-------------------------------------------

To avoid these unnecessary fetches, we add a new subcommand `just
serve` that starts a service that provides the dependencies. This
typically happens by looking up a target-level cache entry. If the
entry, however, is not in cache, this also includes building the
respective `export` target using an associated remote-execution end
point.

### Scope: eligible `export` targets

In order to typically have requests in cache, `just serve` will refuse
to handle requests that do not refer to `export` targets in
content-fixed repositories; recall that for a repository to be content
fixed, so have to be all repositories reachable from there.

### Communication through an associated remote-execution service

Each `just serve` endpoint is always associated with a remote-execution
endpoint. All artifacts exchanged between client and `just serve`
endpoint are exchanged via the CAS that is part in the associated
remote-execution endpoint. This remote-execution endpoint is also used
if `just serve` has to build targets.

The associated remote-execution endpoint can well be the same process
simultaneously acting as `just execute`. In fact, this is the default if
no remote-execution endpoint is specified.

`just serve` will also support the `--endpoint-configuration`
option. As with the default execution endpoint, there is the
understanding that the client uses the same configuration as the
`just serve` endpoint.

### Protocol

Communication is handled via `grpc` exchanging `proto` buffers
containing the information described in the rest of this section.

#### Main request and answer format

A request is given by

 - the map of remote-execution properties for the designated
    remote-execution endpoint; together with the knowledge on the
    fixed endpoint, the `just serve` instance can compute the
    target-level cache shard, and
 - the identifier of the target-level cache key; it is the
    client's responsibility to ensure that the referred blob (i.e.,
    the JSON object with appropriate values for the keys
    `"repo_key"`, `"target_name"`, and `"effective_config"`) as well
    as the indirectly referred repository description (the JSON
    object the `"repo_key"` in the cache key refers to) are uploaded
    to CAS (of the designated remote-execution endpoint) beforehand.

The answer to that request is the identifier of the corresponding
target-level cache value (in the same format as for local
target-level caching). The `just serve` instance will ensure that
the actual value, as well as any directly or indirectly referenced
artifacts are available in the respective remote-execution CAS.
Alternatively, the answer can indicate the kind of error (unknown
root, not an export target, build failure, etc).

#### Auxiliary request: tree of a commit

As for `git` repositories, it is common to specify a commit in order
to fix a dependency (even though the corresponding tree identifier
would be enough). Moreover, the standard `git` protocol supports
asking for the commit of a given remote branch, but additional
overhead is needed in order to get the tree identifier.

Therefore, in order to support clients (or, more precisely,
`just-mr` instances setting up the repository description) in
constructing an appropriate request for `just serve` without
unnecessary overhead, `just serve` will support a second kind of
request, where the client request consists of a `git` commit
identifier and the server answers with the tree identifier for that
commit if it is aware of that commit, or indicates that it is not
aware of that commit.

#### Auxiliary request: describe

To support `just describe` also in the cases where code is delegated
to the `just serve` endpoint, an additional request for the
`describe` information of a target can be requested; as `just
serve` only handles `export` targets, this target necessarily has to
be an export target.

The request is given by the identifier of the target-level cache
key, again with the promise that the referred blob is available in
CAS. The answer is the identifier of a blob containing a JSON object
with the needed information, i.e., those parts of the target
description that are used by `just describe`. Alternatively, the
answer may indicate the kind of error (unknown root, not an export
target, etc).

### Sources: local git repositories and remote trees

A `just serve` instance takes roots from various sources,

 - the `git` repository contained in the local build root,
 - additional `git` repositories, optionally specified in the
   invocation, and
 - as last resort, asking the CAS in the designated remote-execution
   service for the specified `git` tree.

Allowing a list of repositories to take as sources (rather than a single
one) increases the effort when having to search for a specified tree (in
case the requested `export` target is not in cache and an actual
analysis of the build has to be carried out) or specific commit (in case
a client asks for the tree of a given commit). However, it allows for
the natural workflow of keeping separate upstream repositories in
separate clones (updated in an appropriate way) without artificially
putting them in a single repository (as orphan branches).

Supporting building against trees from CAS allows more flexibility in
defining roots that clients do not have to care about. In fact, they can
be defined in any way, as long as

 - the client is aware of the git tree identifier of the root, and
 - some entity ensures the needed trees are known to the CAS.

The auxiliary changes to `just-mr` described later in this document
provide one possible way to handle archives in this way. Moreover, this
additional flexibility will be necessary if we ever support computed
roots, i.e., roots that are the output of a `just` build.

### Absent roots in `just` repository specification

In order for `just` to know for which repositories to delegate the build
to the designated `just serve` endpoint, the repository configuration
for `just` can mark roots as absent; this is done by only giving the
type as `"git tree"` (or the corresponding ignore-special variant
thereof) and the tree identifier in the root specification, but no
witnessing repository.

Any repository containing an absent root has to be content fixed, but
not all roots have to be absent (as `just` can always upload those trees
to CAS). It is an error if, outside the computations delegated to
`just serve`, a non-export target is requested from a repository
containing an absent root. Moreover, whenever there is a dependency on a
repository containing an absent root, a `just
serve` endpoint has to be specified in the invocation of `just`.

### Auxiliary changes

#### `just-mr` pragma `"absent"`

For `just-mr` to know how to construct the repository description,
the description used by `just-mr` is extended. More precisely, a new
key `"absent"` is allowed in the `"pragma"` dictionary of a
repository description. If the specified value is true, `just-mr`
will generate an absent root out of this description, using all
available means to generate that root without ever having to fetch
the repository locally. In the typical case of a `git` repository,
the auxiliary `just serve` function to obtain the tree of a commit
is used. To allow this communication, `just-mr` also accepts the
arguments describing a `just serve` endpoint and forwards them as
early arguments to `just`, in the same way as it does with
`--local-build-root`.

#### `just-mr` to inquire remote execution before fetching

In line with the idea that fetching sources from upstream should
happen only once and not once per developer, we add remote execution
as another way of obtaining files to `just-mr`. More precisely,
`just-mr` will support the options `just` accepts to connect to the
remote CAS. When given, those will be forwarded to `just` as early
arguments (so that later `just`-only ones can override them);
moreover, when a file needed to set up a (present) root is found
neither in local CAS nor in one of the specified distdirs, `just-mr`
will first ask the remote CAS for the missing file before trying to
fetch itself from the specified URL. The rationale for this search
order is that the designated remote-execution service is typically
reachable over the network in a more reliable way than external
resources (while local resources do not require a network at all).

#### `just-mr` to support new repository type `git tree`

A new repository type is added to `just-mr`, called `git tree`. Such
a repository is given by

 - a `git` tree identifier, and
 - a command that, when executed in an empty directory (anywhere in
   the file system) will create in that directory a directory
   structure containing the specified `git` tree (either top-level
   or in some subdirectory). Moreover, that command does not modify
   anything outside the directory it is called in; it is an error
   if the specified tree is not created in this way.

In this way, content-fixed repositories can be generated in a
generic way, e.g., using other version-control systems or
specialized artifact-fetching tools.

Additionally, for archive-like repositories in the `just-mr`
repository specification (currently `archive` and `zip`), a `git`
tree identifier can be specified. If the tree is known to `just-mr`,
or the `"pragma"` `"absent"` is given, it will just use that tree.
Otherwise, it will fetch as usual, but error out if the obtained
tree is not the promised one after unpacking and taking the
specified subdirectory. In this way, also archives can be used as
absent roots.

#### `just-mr fetch` to support storing in remote-execution CAS

The `fetch` subcommand of `just-mr` will get an additional option to
support backing up the fetched information not to a local directory,
but instead to the CAS of the specified remote-execution endpoint.
This includes

 - all archives fetched, but also
 - all trees computed in setting up the respective repository
   description, both, from `git tree` repositories, as well as from
   archives.

In this way, `just-mr` can be used to fill the CAS from one central
point with all the information the clients need to treat all
content-fixed roots as absent.
