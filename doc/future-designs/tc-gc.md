# Target-level cache dependencies for garbage collection

## Background

### Target-level caching

In order to keep analysis manageable, `just` cuts out unchanged
parts of the target graph by means of target-level caching. More
precisely, `export` targets of transitively content-fixed repositories
are cached; if such a target is requested a second time, the cached
value is used without even analysing the target graph defining
this target.

Implicit in that caching is a projection in analysis of this
target from its intensional description to its extensional one.
This change in definition requires the build to be organized in a
way that the intensional and extensional definition are not used
together. Typically, this is achieved by ensuring that whenever an
artifact is used that goes through this `export` target, then so
do all uses; as the change to the extensional name is a projection,
the strictness of the evaluation of `export` targets together with
the fact that (only) on successful build _all_ analysed `export`
targets are cached ensures the absence of conflicts.

#### Example

Consider the following target file (on a content-fixed root) as
example.

```
{ "generated":
  {"type": "generic", "outs": ["out.txt"], "cmds": ["echo Hello > out.txt"]}
, "export": {"type": "export", "target": "generated"}
, "use":
  {"type": "install", "dirs": [["generated", "."], ["generated", "other-use"]]}
, "": {"type": "export", "target": "use"}
}
```

Upon initial analysis (on an empty local build root) of the default
target `""`, the output artifact `out.txt` is an action artifact, more
precisely the same one that is output of the target `"generated"`;
the target `"export"` also has the same artifact on output. After
building the default target, a target-cache entry will be written
for this target, containing the extensional definition of the target,
so for `out.txt` the known artifact `e965047ad7c57865...` stored; as
a side effect, also for the target `"export"` a target-cache entry
will be written, containing, of course, the same known artifact.
So on subsequent analysis, both `"export"` and `""` will still
have the same artifact for `out.txt`, but this time a known one.
This artifact is now different from the artifact of the target
`"generated"` (which is still an action artifact), but no conflicts
arise as the usual target discipline requires that any target not
a (direct or indirect) dependency of `"export"` use the target
`"generated"` only indirectly by using the target `"export"`.

Also note that further exporting such a target has to effect, as a
known artifact always evaluates to itself. In that sense, replacing
by the extensional definition is a projection.

### Gargabe collection

In order to reclaim disk space used by the cache directory, `just`
has an option to carry out garbage collection. More precisely, the
cache is organized in two generations, and a garbage-collection
step removes the old generation and renames the young generation
to be the old one. All operations are carried out from the young
generation; entries found in the old generation are linked to the
young generation before being used. While doing so, the following
invariants are kept by uplinking, in the correct order, more entries.

 - If an artifact is referenced in any cache entry (action cache,
   target-level cache), then the corresponding artifact is in CAS.
 - If a tree is in CAS, then so are its immediate parts (and hence
   also all transitive parts).

## Current situation and shortcomings

As it is implemented currently, garbage collection does not honor
the invariant on export targets that if one export target is in
cache, the ones traversed during the analysis of that particular
target are in cache as well. In fact, those implied targets tend to
be garbage collected, as typical builds only reference the top-level
export targets.

This can lead to a staging conflict, e.g., in the situation where
two `export` targets that contain artifacts from a common `export`
target are used together. Now, if that common `export` target
goes out of cache and for one of the two top-level targets the
description changes, that target will use, due to the cache loss, the
intensional definition of the artifact from the common target with
the other (still cached) target still using the extensional one.

## Proposed solution

We propose to honor the dependency on export targets in garbage
collection by appropriately uplinking the implied target-level cache
entries as well. To do so, the dependency of configured `export`
targets on others will be stored in the corresponding cache value.

### Analysis to track the export targets depended upon

So far, the dependency of export targets on one another was only
tracked implicitly by the evaluation model of target definitions.
As we now have to persist this dependency, we need to explicitly
track it. More precisely, the internal data structure of an analyzed
target will be extended by a set of all the export targets eligible
for caching, represented by their `TargetCacheKey`, encountered
during the analysis of that target.

### Extension of the value of a target-level cache entry

The cached value for a target-level cache entry is serialized as a
JSON object, with currently the keys `"artifacts"`, `"runfiles"`, and
`"provides"`. This object will be extended by an additional (optional)
key `"implied export targets"` that lists (in lexicographic order)
the hashes of the cache keys of the export targets the analysis of
the given export target depends upon; the field is only serialized
if that list is non empty.

### Additional invariant honored during uplinking

Our cache will honor the additional invariant that, whenever a
target-level cache entry is present, so are the implied target-level
cache entries. This invariant will be honored when adding new
target-level cache entries by adding them in the correct order, as
well as when uplinking by uplinking the implied entries first (and
there, of course, honoring the respective invariants).

### Interaction with `just serve`

When building, `just` normally does not create an entry for
target-level cache hit received from `just serve`. However, it
might happen that `just` has to analyse an eligible `export`
target locally, as the `just serve` instance cannot provide it, and
during that analysis `export` targets provided by `just serve` are
encountered. In this case, before writing the target-level cache
entry for the locally analysed `export` target, the `just build`
process, in order to keep cache consistency, will first query and
write to the local target-level cache the transitively implied
target-level cache entries.
