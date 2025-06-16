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

    shutil.copyfile(os.path.join(CONFIG, ".clang-format"), ".clang-format")

    extra = []
    if src.endswith(".tpp"):
        extra = ["-x", "c++"]
    db = [{
        "directory": os.getcwd(),
        "arguments": cmd[:1] + extra + cmd[1:],
        "file": src
    }]
    with open("compile_commands.json", "w") as f:
        json.dump(db, f)

    new_cmd = [
        os.path.join(CONFIG, "toolchain", "bin", "clang-format"),
        "-style=file",
        src,
    ]

    OUT = os.environ.get("OUT")
    if OUT is None:
        print("Failed to get OUT", file=sys.stderr)
        sys.exit(1)
    formatted = os.path.join(OUT, "formatted")
    with open(formatted, "w") as f:
        print("Running cmd %r with db %r" % (new_cmd, db), file=sys.stderr)
        res = subprocess.run(new_cmd, stdout=f).returncode
        if res != 0:
            return res
    return subprocess.run(["diff", "-u", src, formatted]).returncode


if __name__ == "__main__":
    sys.exit(run_lint(sys.argv[1], sys.argv[2:]))
