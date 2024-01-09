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

Core concepts and implementation
--------------------------------

To avoid these unnecessary fetches, we have added a new subcommand 
`just serve` that start a main service that provides the dependencies.
This typically happens by looking up a target-level cache entry.
If the entry, however, is not in cache, this also includes building the
respective `export` target using an associated remote-execution endpoint.

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

### Sources: local git repositories and remote trees

A `just serve` instance takes roots from various sources,

 - the `git` repository contained in the local build root,
 - additional `git` repositories, optionally specified in the
   invocation, and
 - as last resort, asking the CAS in the designated remote-execution
   service for the specified `git` tree.

Allowing a list of repositories to take as sources (rather than a single
one) increases the effort when having to search for a specified tree (e.g.,
in case the requested `export` target is not in cache and an actual
analysis of the build has to be carried out) or specific commit (e.g., in
case a client asks for the tree of a given commit). However, it allows for
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

### Delegation: absent roots in `just` repository specification

In order for `just` to know for which repositories to delegate the build
to the designated `just serve` endpoint, the repository configuration
for `just` can mark roots as _absent_; this is done by only giving the
type as `"git tree"` (or the corresponding ignore-special variant
thereof) and the tree identifier in the root specification, but no
witnessing repository.

Any repository containing an absent root has to be content fixed, but
not all roots have to be absent (as `just` can always upload those trees
to CAS). It is an error if, outside the computations delegated to
`just serve`, a non-export target is requested from a repository
containing an absent root. Moreover, whenever there is a dependency on a
repository containing an absent root, a `just serve` endpoint has to be
specified in the invocation of `just`.

Protocol description
--------------------

Communication is handled via `grpc` exchanging `proto` buffers
containing the information described in the rest of this section.

Besides the main service of `just serve`, auxiliary requests are defined,
bundled in two other services: one allowing `just-mr` to configure
multi-repository builds in the context of `absent` roots, and the other
to perform the optional check for remote-execution endpoint consistency
between a client and the `just serve` endpoint.

### Main service

#### Main request and answer format

A request is given by

 - the map of remote-execution properties for the designated
   remote-execution endpoint,
 - the identifier of the blob containing the endpoint configuration
   information; together with the knowledge on the fixed endpoint,
   the `just serve` instance computes the target-level cache shard,
   and
 - the identifier of the target-level cache key; it is the
   client's responsibility to ensure that the referred blob (i.e.,
   the JSON object with appropriate values for the keys
   `"repo_key"`, `"target_name"`, and `"effective_config"`) as well
   as the indirectly referred repository description (the JSON
   object the `"repo_key"` in the cache key refers to) are uploaded
   to CAS (of the designated remote-execution endpoint) beforehand.

The answer to that request is the identifier of the corresponding
target-level cache value (in the same format as for local
target-level caching). The `just serve` instance ensures that
the actual value, as well as any directly or indirectly referenced
artifacts are available in the respective remote-execution CAS.
Alternatively, the answer indicates the kind of error (unknown
root, not an export target, build failure, etc).

#### Auxiliary request: flexible variables of an `export` target

To allow `just` to compute the target-level cache key without
knowledge of an absent tree, `just serve` also answers questions
about the flexible variables of an `export` target. Such an `export`
target is specified by the tree of its target-level root, the name
of the targets file, and the name of the target itself. The answer
is a list of strings, naming the flexible variables.

#### Auxiliary request: rule description of an `export` target

To support `just describe` also in the cases where code is delegated
to the `just serve` endpoint, an additional request for the
`describe` information of a target can be requested; as `just serve`
only handles `export` targets, this target necessarily has to be an
export target.

The request again contains the tree identifier of the target-level
root, the name of the targets file, and the name of the target to
inspect. The answer is the identifier of a blob containing a JSON object
with the needed information, i.e., those parts of the target description
that are used by `just describe`. Alternatively, the answer indicates
the kind of error (unknown root, not an export target, etc).

### Auxiliary service: source trees

#### Auxiliary request: tree of a commit

For `git` repositories it is common to specify a commit in order
to fix a dependency (even though the corresponding tree identifier
would be enough). Moreover, the standard `git` protocol supports
asking for the commit of a given remote branch, but additional
overhead is needed in order to get the tree identifier.

Therefore, in order to support clients (or, more precisely,
`just-mr` instances setting up the repository description) in
constructing an appropriate request for `just serve` without
unnecessary overhead, `just serve` supports a second kind of
request, where the client request consists of a `git` commit
identifier and the server answers with the tree identifier for that
commit if it is aware of that commit, or indicates that it is not
aware of that commit.

Optionally, the client can request that `just serve` back up this
tree in the CAS of the associated remote-execution endpoint.

#### Auxiliary request: tree of an archive

For archives typically the `git` blob identifier is given, rather
than the tree. In order to allow `just-mr` to set up a repository
description without fetching the respective archive, `just serve`
supports also a request which, given the blob identifier of an
archive, answers with the respective tree identifier of the unpacked
archive. Here, if `just serve` needs the archive, it can look it
up in its CAS, any of the supplied `git` repositories (where one
might be for archiving of the third-party distribution archives),
and the specified remote-execution endpoint.

The (functional!) association of archive blob identifier to tree
identifier of the unpacked archive is stored in the local build
root and the respective tree is fixed in the `git` repository of
the local build root in the same way as `just-mr` does it. When
answering such a request, that tree map is consulted first (so that
those requests as well can be typically served from cache).

Optionally, the client can request that `just serve` back up this
tree in the CAS of the associated remote-execution endpoint.

#### Auxiliary request: tree of a distdir

For archives we typically require the `git` blob identifier to be
given, thus the tree identifier corresponding to a distdir (i.e.,
a list of distfiles) can always be computed without fetching the
actual archives. 

In order to allow `just-mr` to set up a repository description that
can build against an _absent_ distdir repository root, `just serve`
supports a request which, given a mapping of distfile names to their
content blob identifiers, returns the tree identifier of a directory
containing that list of distfiles, with the guarantee that all content
blobs are known to `just serve`. The locations they are looked for are,
in order: the local CAS, all known Git repositories (including the
local Git cache), and the CAS of the associated remote-execution endpoint.
Any blob located in a Git repository is made available in the local CAS.

Optionally, the client can request that `just serve` back up this
tree and all the content blobs in the CAS of the associated
remote-execution endpoint.

#### Auxiliary requests: known Git objects

For `just fetch` operations typically either a blob (e.g., content of
an archive) or a tree (e.g., a root, like from a `git tree` repository)
are needed to be stored into local CAS. For these cases, two auxiliary
requests, one for blobs and one for trees, respectively, have been
provided. They check whether the `just serve` endpoint knows these Git
objects and, if yes, ensure they are uploaded to the remote CAS, from
where the client can easily then retrieve them.

#### Auxiliary requests: check and get trees

In `just-mr`, the `to_git` pragma most often is used to make sure a local
root is available in a content-defined manner as a Git-tree. This allows
it to be used in the build description of export targets. It would be
beneficial then for export targets built by a serve endpoint to have access
to such roots, which often describe exactly the target(s) we want built.
In order to facilitate this, two more auxiliary requests are added.

The first request supported, given a tree identifier corresponding to a
root, checks whether the serve endpoint has _direct_ access to it, meaning
it is available to it locally, and, if found, it makes sure the tree is
made available in a location where the serve endpoint can build against it.
The places checked for this tree are, in order: the Git cache, the known
Git repositories, and the local CAS.

The second request supported, given a tree identifier, retrieves a tree
from the CAS of the associated remote-execution endpoint and makes it
available in a location where the serve endpoint can build against it,
with the understanding that the client has ensured beforehand that the
tree exists in the remote CAS. This is because, in the typical use case,
a client will first perform a check via the first request above, and if
the serve endpoint reports that it doesn't know the tree, the client can
upload it to the remote CAS and ask the serve endpoint to retrieve it via
the second request. This ensures that a client can avoid unnecessary uploads
to the remote CAS, while making sure that the serve endpoint has the roots
marked absent available to build against.

### Auxiliary service: configuration

#### Auxiliary request: remote-execution endpoint

Given that all artifact exchanges between client and `just serve`
rely on the CAS of a given remote endpoint, the client might want
to double check that the remote execution endpoint it wants to use
is the same that is associated with the `just serve` instance.

The server replies with the address (in the usual `HOST:PORT` string
format) of the associated remote execution endpoint, if set, or an
empty string otherwise (i.e., if the serve endpoint acts also as
execution endpoint).

Auxiliary changes
-----------------

### Modifications to the justbuild analysis of an export target

During the analysis of an export target, querying the `just serve` endpoint
is exclusively linked to the presence of at least one _absent_ root.

The first time that we need to query `just serve` we verify that its remote
endpoint coincides with the one given to `just`.

If the _target root_ for this export target is marked as absent:
 - We query the `just serve` for retrieving the flexible configuration
   variables needed to compute the target cache key. If `just serve` cannot
   answer, we break the analysis and inform the user with a proper error
   message.

 - With the served flexible configuration variables we compute the target
   cache key, as all other required information for this in available
   locally. If the cache entry is not in the local target cache, we query
   `just serve` to provide the associated target cache value. If it is not
   able to provide the target cache value, analysis fails and we error out.

It has to be noted that, in the case the `just serve` endpoint also does
not have the target cache entry in its own target cache, a build of the
content-fixed target is dispatched to the associated remote-execution
endpoint, which will thus increase the time spent in the analysis phase,
as experienced by the user. In order to provide a better user experience,
the work done by the `just serve` endpoint is also being reported to the
end user, similarly to the reporting done for a locally-triggered build.

#### `just-mr` pragma `"absent"`

For `just-mr` to know how to construct the multi-repository description,
the description used by `just-mr` was extended. More precisely, a new
key `"absent"` is allowed in the `"pragma"` dictionary of a
repository description. If the specified value is true, `just-mr`
generates an absent root out of this description, using all
available means to generate that root without ever having to fetch
the repository locally. For example, in the typical case of a `git`
repository the auxiliary `just serve` function to obtain the tree of a
commit is used. To allow this communication, `just-mr` also accepts
arguments describing a `just serve` endpoint and forwards them as
early arguments to `just`, in the same way as it does, e.g., with
`--local-build-root`.

If a `just serve` endpoint is given to `just-mr`, the tool ensures
however possible that all absent roots it generates are available also to
the serve endpoint for a subsequent orchestrated remote build. Absent
roots without providing a serve endpoint can also be generated, however
this is not a typical use case and the tool provides warnings in this
regard.

#### `just-mr` to inquire remote execution before fetching

In line with the idea that fetching sources from upstream should
happen only once and not once per developer, we have added remote
execution as another way of obtaining files to `just-mr`. More precisely,
`just-mr` now supports the options `just` accepts to connect to the
remote CAS. When given, those are forwarded to `just` as early
arguments (so that later `just`-only ones can override them);
moreover, when a file needed to set up a (present) root is found
neither in local CAS nor in one of the specified distdirs, `just-mr`
first asks the remote CAS for the missing file before trying to
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
