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

    META = os.environ.get("META")
    if META is None:
        print("Failed to get META", file=sys.stderr)
        return 1

    direct: List[str] = []
    with open(META) as f:
        direct = json.load(f)["direct deps artifact names"]

    include_dirs: List[str] = []
    for i in range(len(cmd)):
        if cmd[i] in ["-I", "-isystem"]:
            include_dirs += [cmd[i + 1]]

    with open(src) as f:
        lines = f.read().splitlines()

    failed: bool = False

    def include_covered(include_path: str) -> bool:
        for d in direct:
            rel_path = os.path.relpath(include_path, d)
            if not rel_path.startswith('../'):
                return True
        return False

    def handle_resolved_include(i: int, resolved: str) -> None:
        nonlocal failed
        if not include_covered(resolved):
            failed = True
            print("%03d %s" % (i, lines[i]))
            print(
                "    ---> including %r which is only provided by an indirect dependency"
                % (resolved, ))

    def handle_include(i: int, to_include: str) -> None:
        for d in include_dirs:
            candidate = os.path.join(d, to_include)
            if os.path.exists(candidate):
                handle_resolved_include(i, candidate)

    for i in range(len(lines)):
        to_include = None
        if lines[i].startswith('#include "'):
            to_include = lines[i].split('"')[1]
        if lines[i].startswith('#include <'):
            to_include = lines[i].split('<', 1)[1].split('>')[0]
        if to_include:  # if non-empty string
            handle_include(i, to_include)

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(run_lint(sys.argv[1], sys.argv[2:]))
