#!/usr/bin/env python3
# Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import json
import os
import shutil
import subprocess
import sys

from typing import List

CXX_LIB_VERSION = "13.3.0"


def dump_meta(src: str, cmd: List[str]) -> None:
    """Dump linter action metadata for further analysis."""
    OUT = os.environ.get("OUT")
    if OUT:
        with open(os.path.join(OUT, "config.json"), "w") as f:
            json.dump({"src": src, "cmd": cmd}, f)


def run_lint(src: str, cmd: List[str]) -> int:
    """Run the lint command for the specified source file."""
    dump_meta(src, cmd)

    CONFIG = os.environ.get("CONFIG")
    if CONFIG is None:
        print("Failed to get CONFIG", file=sys.stderr)
        return 1

    shutil.copyfile(os.path.join(CONFIG, "iwyu-mapping"), "iwyu-mapping.imp")

    # Currently, .tpp files cannot be processed to produce meaningful suggestions
    # Their corresponding .hpp files are analyzed separately, so just skip
    if src.endswith(".tpp"):
        return 0

    # IWYU doesn't process headers by default
    if src.endswith(".hpp"):
        # To analyze .hpp files, we need to "pack" them into a source file:
        # _fake.cpp includes the header and marks it as associated.
        #   We could simply change the command line, but this may cause
        #   IWYU to search for associated files by default (using stem).
        #   Example:
        #   // src/utils/cpp/gsl.hpp considered a source file:
        #   #include "gsl/gsl" // analysis happens because of names
        #   ...
        TMPDIR = os.environ.get("TMPDIR")
        if TMPDIR:
            fake_stem = os.path.join(TMPDIR, "_fake")
            with open(fake_stem + ".cpp", "w+") as fake:
                content = "#include \"%s\" // IWYU pragma: associated" % (
                    src.split(os.path.sep, 1)[1])
                fake.write(content)
                src = os.path.realpath(fake.name)
        else:
            print("Failed to get TMPDIR", file=sys.stderr)
            return 1

        # The command line must be adjusted as well
        e_index = cmd.index("-E")
        cmd = cmd[:e_index] + [
            "-c", src, "-o",
            os.path.realpath(fake_stem) + ".o"
        ] + cmd[e_index + 2:]

    iwyu_flags: List[str] = [
        "--cxx17ns",  # suggest the more concise syntax introduced in C++17
        "--no_fwd_decls",  # do not use forward declarations
        "--no_default_mappings",  # do not add iwyu's default mappings
        "--mapping_file=iwyu-mapping.imp",  # specifies a mapping file
        "--error",  # return 1 if there are any suggestions
        "--verbose=2",  # provide explanations
        "--max_line_length=1000"  # don't limit explanation messages
    ]

    iwyu_options: List[str] = []
    for option in iwyu_flags:
        iwyu_options.append("-Xiwyu")
        iwyu_options.append(option)

    # add include paths from the bundled toolchain
    baseincludepath = os.path.join(CONFIG, "toolchain", "include", "c++",
                                   CXX_LIB_VERSION)
    # We're using the native toolchain, so arch-specific headers are
    # only available for one arch. Hence we can try all supported candidates
    # and add the ones found
    for arch in ["x86_64", "arm"]:
        idir = os.path.join(baseincludepath, "%s-pc-linux-gnu" % (arch, ))
        if os.path.exists(idir):
            iwyu_options += ["-isystem", idir]
    iwyu_options += [
        "-isystem",
        baseincludepath,
    ]

    iwyu_options.extend(cmd[1:])

    # IWYU uses <> for headers from -isystem and quoted for -I
    # https://github.com/include-what-you-use/include-what-you-use/issues/1070#issuecomment-1163177107
    # So we "ask" IWYU here to include all non-system dependencies with quotes
    iwyu_options = ["-I" if x == "-isystem" else x for x in iwyu_options]

    new_cmd = [os.path.join(CONFIG, "toolchain", "bin", "include-what-you-use")]
    new_cmd.extend(iwyu_options)
    new_cmd.append(src)

    print("Running cmd %r" % (new_cmd), file=sys.stderr)
    return subprocess.run(new_cmd, stdout=subprocess.DEVNULL).returncode


if __name__ == "__main__":
    sys.exit(run_lint(sys.argv[1], sys.argv[2:]))
