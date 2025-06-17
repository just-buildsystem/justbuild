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

import os
import sys
import json
import difflib
from functools import cmp_to_key
from typing import Any, Dict, List, Tuple, Union, cast
from argparse import ArgumentParser
try:
    from pygments import console  # type: ignore
    has_pygments = True
except ImportError:
    has_pygments = False

JSON = Union[str, int, float, bool, None, Dict[str, 'JSON'], List['JSON']]


def is_simple(entry: JSON) -> bool:
    if isinstance(entry, list):
        return len(entry) == 0
    if isinstance(entry, dict):
        return len(entry) == 0
    return True


def is_short(entry: JSON, indent: int) -> bool:
    return len(json.dumps(entry)) + indent < 80


def hdumps(entry: JSON, *, _current_indent: int = 0) -> str:
    if is_short(entry, _current_indent):
        return json.dumps(entry)
    if isinstance(entry, list) and entry:
        result = "[ " + hdumps(entry[0], _current_indent=_current_indent + 2)
        for x in entry[1:]:
            result += "\n" + " " * _current_indent + ", "
            result += hdumps(x, _current_indent=_current_indent + 2)
        result += "\n" + " " * _current_indent + "]"
        return result
    if isinstance(entry, dict) and entry:
        result = "{ "
        is_first = True
        for k in entry.keys():
            if not is_first:
                result += "\n" + " " * _current_indent + ", "
            result += json.dumps(k) + ":"
            if is_simple(entry[k]):
                result += " " + json.dumps(entry[k])
            elif is_short(entry[k], _current_indent + len(json.dumps(k)) + 4):
                result += " " + json.dumps(entry[k])
            else:
                result += "\n" + " " * _current_indent + "  "
                result += hdumps(entry[k], _current_indent=_current_indent + 2)
            is_first = False
        result += "\n" + " " * _current_indent + "}"
        return result
    return json.dumps(entry)


def compare_deps(lhs: JSON, rhs: JSON) -> int:
    # Regular strings appear before everything else
    if isinstance(lhs, str) != isinstance(rhs, str):
        if isinstance(lhs, str):
            return -1
        else:
            return 1

    # Regular strings are ordered in the alphabetic order
    if isinstance(lhs, str) and isinstance(rhs, str):
        if lhs < rhs:
            return -1
        elif lhs > rhs:
            return 1

    if isinstance(lhs, list) and isinstance(rhs, list):
        # Third-party dependencies appear before other dependencies
        if cast(Any, lhs[0]) != cast(Any, rhs[0]):
            if lhs[0] == "@":
                return -1
            if rhs[0] == "@":
                return 1

        # Dependencies are ordered in the alphabetic order
        if lhs < rhs:
            return -1
        elif lhs > rhs:
            return 1
    return 0


def sort_list_of_dependencies(deps: JSON) -> JSON:
    if not isinstance(deps, list):
        return deps
    deps = sorted(deps, key=cmp_to_key(compare_deps))

    # Remove duplicated dependencies
    i = 0
    while i < len(deps) - 1:
        if deps[i] == deps[i + 1]:
            deps.pop(i + 1)
        else:
            i += 1
    return deps


# Get indices of intersecting entries in two sorted lists( [[lhs_index, rhs_index],...] )
# Resulting pairs are ordered in the non-descending order for both lhs and rhs.
def get_intersecting_indices(lhs: JSON, rhs: JSON) -> list[Tuple[int, int]]:
    if not isinstance(lhs, list) or not isinstance(rhs, list):
        return list()
    intersection_indices: list[Tuple[int, int]] = list()
    i = 0
    j = 0
    while i < len(lhs) and j < len(rhs):
        compare_result = compare_deps(lhs[i], rhs[j])
        if compare_result < 0:
            i += 1
        elif compare_result > 0:
            j += 1
        else:
            intersection_indices.append((i, j))
            j += 1
    return intersection_indices


def sort_dependencies(content: dict[str, JSON]) -> JSON:
    if "deps" in content:
        content["deps"] = sort_list_of_dependencies(content["deps"])
    if "private-deps" in content:
        content["private-deps"] = sort_list_of_dependencies(
            content["private-deps"])

    # Remove intersecting dependencies between public and private dependencies,
    # if both are present in the target:
    # Typically an intersection occurs when a developer makes a private
    # dependency public, but forgets to remove the private-deps entry.
    # That's why private-deps entries are deleted here.
    if "deps" in content and "private-deps" in content:
        intersection = get_intersecting_indices(content["deps"],
                                                content["private-deps"])
        for _, j in reversed(intersection):
            cast(list[JSON], content["private-deps"]).pop(j)
    return content


def sort_targets_dependencies(data: str) -> str:
    targets: dict[str, JSON] = json.loads(data)
    for target_name, content in targets.items():
        targets[target_name] = sort_dependencies(cast(Dict[str, JSON], content))
    return json.dumps(targets)


def color_diff(before: str, after: str):
    next_lines = 0
    lines: List[str] = []
    for line in difflib.ndiff(before.splitlines(keepends=True),
                              after.splitlines(keepends=True)):
        if line.startswith('+'):
            next_lines = 3
            prev_lines = lines[-3:]
            lines.clear()
            yield "".join(prev_lines + [
                console.colorize("green", line)  # type: ignore
                if has_pygments else line
            ])
        elif line.startswith('-'):
            next_lines = 3
            prev_lines = lines[-3:]
            lines.clear()
            yield "".join(prev_lines + [
                console.colorize("red", line)  # type: ignore
                if has_pygments else line
            ])
        elif line.startswith('?'):
            next_lines = 3
            prev_lines = lines[-3:]
            lines.clear()
            yield "".join(prev_lines + [
                console.colorize("blue", line)  # type: ignore
                if has_pygments else line
            ])
        else:
            if next_lines > 0:
                next_lines -= 1
                yield line
            else:
                lines.append(line)


if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument("in_file",
                        help="input file (omit for stdin)",
                        nargs='?',
                        default=None)
    parser.add_argument("-c",
                        "--check",
                        action='store_true',
                        help="just verify format of input",
                        default=False)
    parser.add_argument("-d",
                        "--diff",
                        action='store_true',
                        help="with -c, print colored diff",
                        default=False)
    parser.add_argument("-i",
                        "--in-place",
                        action='store_true',
                        help="modify input file in-place",
                        default=False)
    parser.add_argument("-s",
                        "--sort",
                        action='store_true',
                        help="sort dependencies and remove duplicates",
                        default=False)

    options = parser.parse_args()

    if options.in_file:
        with open(options.in_file, 'r') as f:
            data = f.read()
    else:
        data = sys.stdin.read()

    try:
        data_formatted: str = data
        if options.sort:
            data_formatted = sort_targets_dependencies(data_formatted)
        data_formatted: str = hdumps(json.loads(data_formatted))
    except Exception as e:
        # if the file contains syntax errors, we print the file name
        if options.in_file:
            print("Found syntax issues in", options.in_file)
        print(e)
        exit(1)

    data_newline = data_formatted + '\n'

    if options.check:
        if data != data_newline:
            print("Found format issues" + (
                (" in file: " + options.in_file) if options.in_file else ":"))
            if options.diff:
                print("".join(color_diff(data, data_newline)))
            exit(1)
        exit(0)

    if options.in_place and options.in_file:
        out_file = f"{options.in_file}.out"
        with open(out_file, 'w') as f:
            f.write(data_newline)
        os.rename(out_file, options.in_file)
        exit(0)

    print(data_formatted)
