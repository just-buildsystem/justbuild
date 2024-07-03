# Living with hardlink limit

## Current state and shortcomings

The build tool stores all files in a content-addressable store.
For each action, a fresh action directory is created in input files
are hard-linked from the content-addressable store to the action
directory. Output files are linked back into the content-addressable
store (after fixing permissions and time stamp) before the action
directory is removed.

This has the consequence that the link count of a file in CAS
is (essentially) the number of times a file with that content is
in use in currently running actions. Now, on some file systems
the maximal link count can be as low as 2**16, i.e., 64k. However,
it is not an uncommon request to rerun certain test cases 100k
times (especially when using remote execution) to look for rare
race conditions. In this case, the action summarizing the various
test runs (which is especially important for that large number of
tests) has all the test output and results as input, including a
large number of test runs with the same output state (like `PASS`),
thus hitting the maximal link count.

## Proposed solution

### Changes to the setup of local actions

So far, when creating a link as part of setting up a local action
fails, the whole setup up aborted (see `LocalAction::StageInput`)
and the `LocalAction::Run` method returns `std::nullopt` indicating
that it failed to run the action at all. This will be changed in
the following way.

 - The `FileManager::CreateHardLink` function distinguishes in
   its return value between failure due to reaching the hardlink
   limit (indicated by `errno` being set to `EMLINK` in the underlying
   `link`(2) call) and other forms of failure.

 - When creation of a hard link fails in `LocalAction::StageInput`
   due to the hardlink limit, a copy of the file is created with
   correct permission and timestamp (and, if executable, in a dedicated
   subprocess to avoid file descriptors to the file outliving the
   copying); this copy is created in a temporary directory with life
   time being the creation of the action directory (or reaching of
   the hardlink limit again, whatever is earlier, see next item).
   This copy is then hard linked into the action directory.

 - To avoid copying the same file over and over again, a reference to
   mutable map in the scope of `LocalAction::CreateDirectoryStructure`
   is passed to all calls of `LocalAction::StageInput`. This map maps
   `Artifact::ObjectInfo` to the copy of the file to be used, if it
   was copied already; it also keeps the ephemeral directories for
   those copies alive. When creating a hardlink fails, regardless
   of whether the file to be hardlinked is in CAS or already a
   temporary copy, a copy is created and the entry in that map
   updated accordingly. This might get some ephemeral directories
   out of scope (and hence being cleaned up) that host a file where
   the hardlink limit is already reached; the exisitng hard links
   will keep the file alive on disk as long as needed. As creating
   the action directory is a single-threaded operation, this inplace
   update of the map to file copies is a safe operation.

### Rules changes

The changes to the way justbuild handles the setup of action
directories works around the hardlink limit for actions executed
by justbuild itself, i.e., for local execution and actions executed
by a `just execute` instance. It does not change, however, the
way other remote-execution services work. So, in order to allow
delegating action that are expected to require large link counts (like
the test-summary action) to a suitable execution endpoint, rules
will honor appropriate configuration variables to add additional
remote-execution properties to those actions, in the same way, our
typesetting rules do this already.
