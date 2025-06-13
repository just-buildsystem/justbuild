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

CXX_LIB_VERSION = "13.3.0"

def dump_meta(src, cmd):
    OUT = os.environ.get("OUT")
    if OUT:
        with open(os.path.join(OUT, "config.json"), "w") as f:
            json.dump({"src": src, "cmd": cmd}, f)


def run_lint(src, cmd):
    dump_meta(src, cmd)
    config = os.environ.get("CONFIG")
    shutil.copyfile(os.path.join(config, ".clang-tidy"), ".clang-tidy")
    extra = [ "-Wno-unused-command-line-argument"]

    # add include paths from the bundled toolchain
    baseincludepath = os.path.join(
        config, "toolchain", "include", "c++", CXX_LIB_VERSION
    )
    # We're using the native toolchain, so arch-specific headers are
    # only available for one arch. Hence we can try all supported candidates
    # and add the ones found
    for arch in ["x86_64", "arm"]:
        idir = os.path.join(baseincludepath,
                            "%s-pc-linux-gnu" % (arch,))
        if os.path.exists(idir):
            extra += ["-isystem", idir]
    extra += [
        "-isystem", baseincludepath,
    ]

    if src.endswith(".tpp"):
        extra += ["-x", "c++"]
    db = [{
        "directory": os.getcwd(),
        "arguments": cmd[:1] + extra + cmd[1:],
        "file": src
    }]
    with open("compile_commands.json", "w") as f:
        json.dump(db, f)
    new_cmd = [
        os.path.join(config, "toolchain", "bin", "clang-tidy"),
        src,
    ]
    print("Running cmd %r with db %r" % (new_cmd, db), file=sys.stderr)
    return subprocess.run(new_cmd).returncode


if __name__ == "__main__":
    sys.exit(run_lint(sys.argv[1], sys.argv[2:]))
