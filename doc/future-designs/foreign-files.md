# Foreign-File Roots

## Current state and shortcomings

Currently, `just-mr` supports defining roots by `git` commits,
archives with given archive content, and, as universal interface,
cat given `git tree` together with instructions on how to create
it. While `git tree` is universal, the more specialized roots given
by `git` commits use the knowledge of the special nature to support
downloading from mirrors; for archives there is also the option of
fetching them ahead of time and providing `just-mr` with a location
where to find them by giving the `--distdir` option.

Besides archives, there are also other files that are desirable
to be fetched, like patches, data files, and test scripts. Also
for those files, it is desirable to have the benefits of mirrors
and ahead-of-time fetching which cannot be provided by the generic
`git tree` roots.

## Proposed solution

We propose to allow an additional type of root in `just-mr`'s
repository configuration, called `"foreign file"`. It is given by
 - the information describing the file to fetch in the same way as
   for archive roots, i.e.,
   - `"content"` and `"fetch"`,
   - as well as optionally also `"distfile"`, `"mirrors"`, `"sha256"`,
     and `"sha512"`
   with the same semantics as for archives, and
 - the information where to place that file in the otherwise empty
   root, more precisely
   - `"name"` describing a valid file name, and
   - the optional boolean (defaulting to `false`) `"executable"`
     indicating whether the file should be placed there with the
     executable bit set.
The foreign file will be fetched in the same way as archives are
fetched. Instead of unpacking, the root is created by taking the tree
with a single entry consisting of the given file placed at the given
name with the given executable bit. Such a root can meaningfully
be used, as the target files can be provided in a different root.

For `"foreign file"` roots, `just-mr` interacts with `just serve`
in the same way as for `"distdir"` roots. More precisely,
 - the (internal, not-yet released) `ServeDistdirTree` RPC and the
   corresponding messages will be renamed to `ServeFileTree` and
   used for `"foreign file"` roots that are absent, and
 - for realizing `"foreign file"` roots locally, the `ServeContent`
   RPC is used.
