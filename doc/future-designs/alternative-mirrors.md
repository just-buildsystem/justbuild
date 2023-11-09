Alternative Mirrors Proposal
============================

Background
----------

The repository fetch tool `just-mr` reads a repository-configuration
file that describes `git` repositories and repositories given by
archives in a content-addressable way: by the `git` commit id, or
the blob identifier of the archive, respectively. Hence it does
not matter where these resources are fetched form; nevertheless,
a location to fetch from is needed for `just-mr` to be able to set
up those repositories. Typically the main upstream URL is given.

While `just-mr` fetches each archive or commit only once and keeps
a local copy, large organizations still tend to set up a local mirror.
In this way, not every person working on the project has to fetch
from upstream (thus reducing load there) and there is a central
place archiving all the dependencies. Such an archive is needed
anyway, for audit and compliance reasons, as well as to be able
to continue the project independent of the dependencies' upstream.

It is therefore desirable that `just-mr` support this workflow while
still clearly pointing out the upstream location, e.g., for updating.

Proposal
--------

We propose the following, backwards-compatible, extensions to
`just-mr`.

### Mirrors field in `just-mr` repository config

In the multi-repository configuration, in the definition of an
`"archive"`, `"zip"` or `"git"` repository, an addition field
`"mirrors"` can be added. If given, this field has to be a list
of URLs that provide additional places where the respective
repository can be fetched from. `just-mr` will only consider a fetch
failed if the repository cannot be fetched neither from the main
location (described in `"fetch"` field for `"archive"` and `"zip"`
repositories, and `"repository"` field for `"git"` repositories),
nor from any of the specified mirrors.

### Additional mirrors in the just local specification

Local mirrors of organizations are often not available to the
general public and possibly not even available to everyone who
has access to the respective project. In order to avoid polluting
a multi-repository specification with the URLs of such restricted
mirrors, the `.just-local` file is extended to support additional
keys in its JSON object.
- For the optional key `"local mirrors"`, if given, a JSON object
  is specified that maps primary URLs to a list of local (non-public)
  mirrors. Those mirrors are always tried first (in the given order)
  before any other URL is contacted.
- For the optional key `"preferred hostnames"`, if given, a list of
  host names is specified. When fetching from a non-local URL, URLs
  with host names in the given list are preferred (in the order
  given) over URLs with other host names.


### `just-import-git` to support mirror specifications

As multi-repository specifications are often generated from a
description of the local repositories by a chain of `just-import-git`
invocations, this tool needs to be able to also insert a `"mirrors"`
field for the repository imported. Therefore, this tool will be
extended by an option `--mirror` where multiple occurrences accumulate
to specify additional mirrors for the URL fetched. These mirrors,
if any, form the `"mirrors"` field of the repository imported; they
are not used for the fetching in the import step itself, as the
head commit of the primary URL is to be taken.
