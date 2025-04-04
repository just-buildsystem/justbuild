Implementing debug fission in the C/C++ rules
=============================================

Motivation
----------

Building with debug symbols is needed for debugging purposes, however it
results in artifacts that are many times larger than their release versions.
In general, debug builds are also slower than release builds due to various
reasons, the main ones being the disabling of certain code optimizations (in
order to allow debuggers to properly work) and debug-only checks and
diagnostics. Furthermore, sections of debug symbols from common dependencies can
be replicated many times between different artifacts, but also inside single
artifacts.

In the cases where one does not produce separate release and debug versions,
it is usual to just generate the debug artifacts, then strip the debug symbols
from them to obtain a pseudo-release version. Even in this case, being able to
reduce the time and space requirements for producing the debug artifacts in the
first place is of value.

Moreover, distributions usually provide debug information in different packages
and for that purpose apply themselves debug fission or stripping techniques.
Projects that provide separated debug information have thus an advantage in
getting accepted by reducing the work involved in packaging them for distros.

[Debug fission](https://www.tweag.io/blog/2023-11-23-debug-fission)
-------------------------------------------------------------------

This approach targets specifically Linux ELF files and `gdb(1)`. This is,
however, more than enough to cover the most used UNIX-like platforms and most
general purpose debugging tools, which rely on `gdb(1)`.

### Concept

Fission, aka splitting debug information into separate files, exists in modern
tools for a [long time](https://gcc.gnu.org/wiki/DebugFission). This method
can be applied when one already splits builds into their constituting compile
and link steps, which is the case with most modern build tools, including
*justbuild*. Debug fission proposes that the compilation step of a debug build
produce, instead of one object file, two artifacts: a `.dwo` DWARF file
containing all the debug symbols of the compilation unit, and the (now smaller)
`.o` object file containing now only references to the debug symbols from the
`.dwo` file. These object files can be linked as usual to produce the final
build artifact, and the `.dwo` files can be either retained as-is, or packed
into a corresponding `.dwp` file.

### Benefits

By splitting the debug symbols of each compilation unit into separate
artifacts, these can be cached and reused as needed, removing any previous
debug symbols duplication across build artifacts. This has also a beneficial
impact on the build times. Moreover, the stripped artifacts are quasi-identical
(e.g., executables differ only in their internal Build ID).

Proposal
--------

Debug fission requires changes affecting the `OSS` and `rules-cc` rules, as well
as the OSS toolchain configuration. In order to ensure that new and old
toolchains and rules, respectively, are still able to work together, the new
rules will only perform debug fission if the toolchain is configured to use this
feature.

The following sections describe the needed changes in detail.

### Extend `["CC", "defaults"]` rule with new fields

The `["CC", "defaults"]` should accept 3 new fields:

 - `"DWP"`, containing as a singleton list the path to the `dwp` tool to be used
   for packing DWARF files. This field should be handled the same as, e.g., the
   `"CC"` variable. The description of our toolchains should be extended with
   the `"DWP"` field, pointing to the location of the respective tool in the
   staged binaries folder.

 - `"ADD_DEBUGFLAGS"`, containing as a list compile flags to be appended to both
   C and C++ flags for debug builds. As with other `"ADD_<X>FLAGS"` variables,
   these extend the existing flags.

 - `"DEBUGFLAGS"`, containing as a list the compile flags to be used for debug
   builds, replacing existing.

#### Change `"DEBUG"` configuration variable value type to map

In order for the defaults to properly set the appropriate flags in debug mode,
the configuration variable `"DEBUG"` is changed to be a mapping. As before, a
`true` evaluation of its value (now, a not empty map) will signal debug mode.
The following supported keys are proposed:

 - a `"USE_DEBUG_FISSION"` flag, which, if evaluated to `true` enables debug
   fission and otherwise signals regular debug mode, and

 - a `"FISSION_CONFIG"` map, which can configure in more detail how debug
   fission behaves. Existence is enforced if debug fission is enabled.
   Otherwise, this field is ignored.

The `"USE_DEBUG_FISSION"` flag only enables the corresponding rules logic, but
does not change any compile and/or link flags. In particular, instructing the
compiler to generated any DWARF files to begin with must be done through an
appropriate `"FISSION_CONFIG"` map.

The `"FISSION_CONFIG"` map should accept the following keys:

 - `"USE_SPLIT_DWARF"`: If evaluated to `true`, appends the `-gsplit-dwarf`
   flag to the compile flags. This is currently the only flag that actually
   instructs the compiler to generate .dwo files and as such it should be
   provided in case debug fission is enabled.

 - `"DWARF_VERSION"`: Expects a string defining the DWARF format version. If
   provided, appends the `-gdwarf-<value>` flag to the compile flag.

   Each toolchain comes with a default in terms of which version of the DWARF
   format is used. Basically all reasonably modern toolchains (GCC >=4.8.1,
   Clang >=7.0.0 at least) and debugging tools (GDB >= 7.0) use DWARFv4 by
   default, with the more recent versions having already switched to using the
   newer, upward compatible [DWARFv5](https://dwarfstd.org/dwarf5std.html)
   format. However, the degree of implementation and default support of the
   various compilers and tools differs, so it is recommended to use version 4.

 - `"USE_GDB_INDEX"`: If evaluated to `true`, appends the `-Wl,--gdb-index` flag
   to the compile flags. Defaults to `false`.

   This option enables, in linkers that support it, an optimization which
   bundles certain debug symbols sections together into a single `.gdb_index`
   section, reducing the size of the final artifact (quite significantly for
   large artifacts) and drastically improving the debugger start time, but at
   the cost of a slower linking step.

    - Known supported linkers: `lld` (LLVM >=7), `gold` (binutils >=2.24*), `mold` (>=2.3)

      *`gold` linker additional info:
      As per the [release notes](https://lwn.net/Articles/1007541/), `binutils`
      2.44 (2025-02-02) does **NOT** come with the `gold` linker anymore, as it
      is considered deprecated and will be removed completely in the near future
      unless new maintainers are found. Note also that Fedora, for concerns of
      bit-rot, moved the `gold` linker from its `binutils` RPM to a separate
      package already since version 31 (2019-10-29).

    - Known unsupported linkers: `ld` (GNU)

 - `"USE_DEBUG_TYPES_SECTION"`: If evaluated to `true`, appends the
   `-fdebug-types-section` flag to the compile flags. Defaults to `false`.

   This option enables, for toolchains supporting at least DWARFv4, an
   optimization that produces separate debug symbols sections for certain large
   data types, thus providing the linker the opportunity to deduplicate more
   debug information, resulting in smaller artifacts.

   More performant approaches to reduce the size of the debug information exist,
   but are not as straight-forward to implement as enabling a flag. For example,
   Fedora opted instead to use the `dwz` compression tool, another known
   approach, and make it its default for handling debug RPMs already since
   version 18 (2013-01-15).

The `["CC", "defaults"]` rule interrogates these fields in order to set the
appropriate debug flags to be provided to the library/binary rules. Note
that it is the user's responsibility to configure the debug mode accordingly.
It is always up to each toolchain how unsupported or unexpected combinations of
flags are being handled.

### Changes to `"library"` and `"binary"` rules

The toolchain flags will be treated as before, with the addition that in debug
mode, if the final compile flags list is empty, `["-g"]` will be used by
default. To this resulting flags list any debug fission flags configured via the
`"FISSION_CONFIG"` map will be added if debug fission is enabled.

The `"USE_DEBUG_FISSION"` flag of `"DEBUG"` will inform these rules on whether
the debug fission logic should be used or not. In this way, only the combination
of an appropriate configuration and these updated rules will be able to perform
debug fission, while all other combinations of toolchains and rules will perform
as before.

All consumers of the internal `"objects"` expression (i.e., static/dynamic
libraries and binaries) should provide a new field `"dwarf-pkg"`, defaulting to
the empty map. If debug fission is enabled, this field will contain the
corresponding DWARF package file, constructed via a new expression
`"dwarf package"`, which, based on given `"dwarf objects"`, `"dwarf deps"` and a
`stage`, uses the given DWARF objects and DWARF package files (provided by the
`"dwarf deps"`) as inputs to an `ACTION` generating a resulting DWARF package
file, appropriately staged. Each such consumer will pass the appropriate inputs
to the new `"dwarf package"` expression considering that `.dwo` files need to be
gathered the same as `.o` files and `.dwp` files need to be gathered the same as
libraries/binaries. In particular, the `"dwarf deps"` shall contain only the
DWARF package files of any link dependencies that are considered for generating
the regular artifact (library/binary).

As an auxiliary required change, the output of the compile `ACTION` in the
`"objects"` expression should be extended, if debug fission is enabled, by an
additional path corresponding to the expected `.dwo` DWARF file, staged next to
the usual `.o` file. This path needs to be passed to any consumers of
`"objects"`. For this purpose, the expression should be refactored to return a
map result instead of a single variable.

### Extend the `"install-with-deps"` rule to stage DWARF files

The `"install-with-deps"` rule should stage also any `"debug-pkg"` entries from
providers when in debug mode, in the same locations as it does regular
artifacts. As paths are handled by each library/binary accordingly, the DWARF
package files should always end up next to their corresponding build artifact,
i.e., where `gdb(1)` expects them.

### Bootstrappable toolchain to expose the `dwp` binary

The bootstrappable toolchain repository provides several toolchains built from
source. In the case of the `gcc` and `clang` compilers, a `dwp` tool should be
part of the produced staged binaries. This can be advertised to consumers of
these toolchains (compilers, compilers+tools) in their `["CC", "defaults"]` via
the newly introduced `"DWP"` field.
