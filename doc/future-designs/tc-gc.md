# Target-level cache dependencies for garbage collection

## Interaction with `just serve`

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
