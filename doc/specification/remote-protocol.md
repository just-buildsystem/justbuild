Specification of the just Remote Execution Protocol
===================================================

Introduction
------------

just supports remote execution of actions across multiple machines. As
such, it makes use of a remote execution protocol. The basis of our
protocol is the open-source gRPC [remote execution
API](https://github.com/bazelbuild/remote-apis/blob/main/build/bazel/remote/execution/v2/remote_execution.proto).
We use this protocol in a **compatible** mode, but by default, we use a
modified version, allowing us to pass git trees and files directly
without even looking at their content or traversing them. This
modification makes sense since it is more efficient if sources are
available in git repositories and much open-source code is hosted in git
repositories. With this protocol, we take advantage of already hashed
git content as much as possible by avoiding unnecessary conversion and
communication overhead.

In the following sections, we explain which modifications we applied to
the original protocol and which requirements we have to the remote
execution service to seamlessly work with just.

just Protocol Description
-------------------------

### git Blob and Tree Hashes

In order to be able work with git hashes, both client side as well as
server side need to be extended to support the regular git hash
functions for blobs and trees:

The hash of a blob is computed as

    sha1sum(b"blob <size_of_content>\0<content>")

The hash of a tree is computed as

    sha1sum(b"tree <size_of_entries>\0<entries>")

where `<entries>` is a sequence (without newlines) of `<entry>`, and
each `<entry>` is

    <mode> <file or dir name>\0<git-hash of the corresponding blob or tree>

`<mode>` is a number defining if the object is a file (`100644`), an
executable file (`100755`), a tree (`040000`), or a symbolic link
(`120000`). More information on how git internally stores its objects
can be found in the official [git
documentation](https://git-scm.com/book/en/v2/git-Internals-git-Objects).

Since git hashes blob content differently from trees, this type of
information has to be transmitted in addition to the content and the
hash. To this aim, just prefixes the git hash values passed over the
wire with a single-byte marker. Thus allowing the remote side to
distinguish a blob from a tree without inspecting the (potentially
large) content. The markers are

 - `0x62` for a git blob (`0x62` corresponds to the character `b`)
 - `0x74` for a git tree (`0x74` corresponds to the character `t`)

Since hashes are transmitted as hexadecimal string, the resulting length
of such prefixed git hashes is 42 characters. The server side has to
accept this hash length as valid hash length to detect our protocol and
to apply the according git hash functions based on the detected prefix.

### Blob and Tree Availability

Typically, it makes sense for a client to check the availability of a
blob or a tree at the remote side, before it actually uploads it. Thus,
the remote side should be able to answer availability requests based on
our prefixed hash values.

### Blob Upload

A blob is uploaded to the remote side by passing its raw content as well
as its `Digest` containing the git hash value for a blob prefixed by
`0x62`. The remote side needs to verify the received content by applying
the git blob hash function to it, before the blob is stored in the
content addressable storage (CAS).

If a blob is part of git repository and already known to the remote
side, we even do not have to calculate the hash value from a possible
large file, instead we can directly use the hash value calculated by git
and pass it through.

### Tree Upload

In contrast to regular files, which are uploaded as blobs, the original
protocol has no notion of directories on the remote side. Thus,
directories need to be traversed and converted to `Directory` Protobuf
messages, which are then serialized and uploaded as blobs.

In our modified protocol, we prevent this traversing and conversion
overhead by directly uploading the git tree objects instead of the
serialized Protobuf messages if the directory is part of a git
repository. Consequently, we can also reuse the corresponding git hash
value for a tree object, which just needs to be prefixed by `74`, when
uploaded.

The remote side must accepts git tree objects instead `Directory`
Protobuf messages at any location where `Directory` messages are
referred (e.g., the root directory of an action). The tree content is
verified using the git hash function for trees. In addition, it has to
be modified to parse the git tree object format.

Using this git tree representation makes tree handling much more
efficient, since the effort of traversing and uploading the content of a
git tree occurs only once and for each subsequent request, we directly
pass around the git tree id. We require the invariant that if a tree is
part of any CAS then all its content is also available in this CAS. To
adhere to this invariant, the client side has to prove that the content
of a tree is available in the CAS, before uploading this tree. One way
to ensure that the tree content is known to the remote side is that it
is uploaded by the client. The server side has to ensure this invariant
holds. In particular, if the remote side implements any sort of pruning
strategy for the CAS, it has to honor this invariant when an element got
pruned.

Another consequence of this efficient tree handling is that it improves
**action digest** calculation noticeably, since known git trees referred
by the root directory do not need to be traversed. This in turn allows
to faster determine whether an action result is already available in the
action cache or not.

### Tree Download

Once an action is successfully executed, it might have generated output
files or output directories in its staging area on the remote side. Each
output file needs to be uploaded to its CAS with the corresponding git
blob hash. Each output directory needs to be translated to a git tree
object and uploaded to the CAS with the corresponding git tree hash.
Only if the content of a tree is available in the CAS, the server side
is allowed to return the tree to the client.

In case of a generated output directory, the server only returns the
corresponding git tree id to the client instead of a flat list of all
recursively generated output directories as part of a `Tree` Protobuf
message as it is done in the original protocol. The remote side promises
that each blob and subtree contained in the root tree is available in
the remote CAS. Such blobs and trees must be accessible, using the
streaming interface, without specifying the size (since sizes are not
stored in a git tree). Due to the [Protobuf 3
specification](https://protobuf.dev/reference/protobuf/proto3-spec/), which is
used in this remote execution API, not specifying the size means the default
value `0` is used.
