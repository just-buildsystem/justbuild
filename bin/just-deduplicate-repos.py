#!/usr/bin/env python3
# Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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
import sys

from typing import Any, Dict, List, NoReturn, Optional, Tuple, Union, cast

# generic JSON type
Json = Any


def log(*args: str, **kwargs: Any) -> None:
    print(*args, file=sys.stderr, **kwargs)


def fail(s: str, exit_code: int = 1) -> NoReturn:
    log(f"Error: {s}")
    sys.exit(exit_code)


def bisimilar_repos(repos: Json) -> List[List[str]]:
    """Compute the maximal bisimulation between the repositories
    and return the bisimilarity classes."""
    bisim: Dict[Tuple[str, str], Json] = {}

    def is_different(name_a: str, name_b: str) -> bool:
        pos = (name_a, name_b) if name_a < name_b else (name_b, name_a)
        return bisim.get(pos, {}).get("different", False)

    def mark_as_different(name_a: str, name_b: str):
        nonlocal bisim
        pos = (name_a, name_b) if name_a < name_b else (name_b, name_a)
        entry = bisim.get(pos, {})
        if entry.get("different"):
            return
        bisim[pos] = dict(entry, **{"different": True})
        also_different: List[Tuple[str, str]] = entry.get("different_if", [])
        for a, b in also_different:
            mark_as_different(a, b)

    def register_dependency(name_a: str, name_b: str, dep_a: str, dep_b: str):
        pos = (name_a, name_b) if name_a < name_b else (name_b, name_a)
        entry = bisim.get(pos, {})
        deps: List[Tuple[str, str]] = entry.get("different_if", [])
        deps.append((dep_a, dep_b))
        bisim[pos] = dict(entry, **{"different_if": deps})

    def roots_equal(a: Json, b: Json, name_a: str, name_b: str) -> bool:
        if a["type"] != b["type"]:
            return False
        if a["type"] == "file":
            return a["path"] == b["path"]
        elif a["type"] in ["archive", "zip"]:
            return (a["content"] == b["content"]
                    and a.get("subdir", ".") == b.get("subdir", "."))
        elif a["type"] == "git":
            return (a["commit"] == b["commit"]
                    and a.get("subdir", ".") == b.get("subdir", "."))
        elif a["type"] in ["computed", "tree structure"]:
            if a["type"] == "computed":
                if (a.get("config", {}) != b.get("config", {})
                        or a["target"] != b["target"]):
                    return False
            if a["repo"] == b["repo"]:
                return True
            elif is_different(a["repo"], b["repo"]):
                return False
            else:
                # equality pending target repo equality
                register_dependency(a["repo"], b["repo"], name_a, name_b)
                return True
        else:
            # unknown repository type, the only safe way is to test
            # for full equality
            return a == b

    def get_root(repos: Json,
                 name: str,
                 *,
                 root_name: str = "repository",
                 default_root: Optional[Json] = None) -> Json:
        root = repos[name].get(root_name)
        if root is None:
            if default_root is not None:
                return default_root
            else:
                fail("Did not find mandatory root %s" % (name, ))
        if isinstance(root, str):
            return get_root(repos, root)
        return root

    def repo_roots_equal(repos: Json, name_a: str, name_b: str) -> bool:
        if name_a == name_b:
            return True
        root_a = None
        root_b = None
        for root_name in [
                "repository", "target_root", "rule_root", "expression_root"
        ]:
            root_a = get_root(repos,
                              name_a,
                              root_name=root_name,
                              default_root=root_a)
            root_b = get_root(repos,
                              name_b,
                              root_name=root_name,
                              default_root=root_b)
            if not roots_equal(root_a, root_b, name_a, name_b):
                return False
        for file_name, default_name in [("target_file_name", "TARGETS"),
                                        ("rule_file_name", "RULES"),
                                        ("expression_file_name", "EXPRESSIONS")
                                        ]:
            fname_a = repos[name_a].get(file_name, default_name)
            fname_b = repos[name_b].get(file_name, default_name)
            if fname_a != fname_b:
                return False
        return True

    names = sorted(repos.keys())
    for j in range(len(names)):
        b = names[j]
        for i in range(j):
            a = names[i]
            if is_different(a, b):
                continue
            if not repo_roots_equal(repos, a, b):
                mark_as_different(a, b)
                continue
            links_a = repos[a].get("bindings", {})
            links_b = repos[b].get("bindings", {})
            if set(links_a.keys()) != set(links_b.keys()):
                mark_as_different(a, b)
                continue
            for link in links_a.keys():
                next_a = links_a[link]
                next_b = links_b[link]
                if next_a != next_b:
                    if is_different(next_a, next_b):
                        mark_as_different(a, b)
                        continue
                    else:
                        # equality pending binding equality
                        register_dependency(next_a, next_b, a, b)
    classes: List[List[str]] = []
    done: Dict[str, bool] = {}
    for j in reversed(range(len(names))):
        name_j = names[j]
        if done.get(name_j):
            continue
        c = [name_j]
        for i in range(j):
            name_i = names[i]
            if not bisim.get((name_i, name_j), {}).get("different"):
                c.append(name_i)
                done[name_i] = True
        classes.append(c)
    return classes


def dedup(repos: Json, user_keep: List[str]) -> Json:

    keep = set(user_keep)
    main = repos.get("main")
    if isinstance(main, str):
        keep.add(main)

    def choose_representative(c: List[str]) -> str:
        """Out of a bisimilarity class chose a main representative"""
        candidates = c
        # Keep a repository with a proper root, if any of those has a root.
        # In this way, we're not losing actual roots.
        with_root = [
            n for n in candidates
            if isinstance(repos["repositories"][n]["repository"], dict)
        ]
        if with_root:
            candidates = with_root

        # Prefer to choose a repository we have to keep anyway
        keep_entries = set(candidates) & keep
        if keep_entries:
            candidates = list(keep_entries)

        return sorted(candidates, key=lambda s: (s.count("/"), len(s), s))[0]

    def merge_pragma(rep: str, merged: List[str]) -> Json:
        desc = cast(Union[str, Dict[str, Json]],
                    repos["repositories"][rep]["repository"])
        if not isinstance(desc, dict):
            return desc
        pragma = desc.get("pragma", {})
        # Clear pragma absent unless all merged repos that are not references
        # have the pragma
        absent: bool = pragma.get("absent", False)
        for c in merged:
            alt_desc = cast(Union[str, Dict[str, Json]],
                            repos["repositories"][c]["repository"])
            if (isinstance(alt_desc, dict)):
                absent = \
                    absent and alt_desc.get("pragma", {}).get("absent", False)
        pragma = dict(pragma, **{"absent": absent})
        if not absent:
            del pragma["absent"]
        # Add pragma to_git if at least one of the merged repos requires it
        to_git = pragma.get("to_git", False)
        for c in merged:
            alt_desc = cast(Union[str, Dict[str, Json]],
                            repos["repositories"][c]["repository"])
            if (isinstance(alt_desc, dict)):
                to_git = \
                    to_git or alt_desc.get("pragma", {}).get("to_git", False)
        pragma = dict(pragma, **{"to_git": to_git})
        if not to_git:
            del pragma["to_git"]
        # Update the pragma
        desc = dict(desc, **{"pragma": pragma})
        if not pragma:
            del desc["pragma"]
        return desc

    bisim = bisimilar_repos(repos["repositories"])
    renaming: Dict[str, str] = {}
    updated_repos: Json = {}
    for c in bisim:
        if len(c) == 1:
            continue
        rep = choose_representative(c)
        updated_repos[rep] = merge_pragma(rep, c)
        for repo in c:
            if ((repo not in keep) and (repo != rep)):
                renaming[repo] = rep

    def final_root_reference(name: str) -> str:
        """For a given repository name, return a name than can be used
        to name root in the final repository configuration."""
        root: Json = repos["repositories"][name]["repository"]
        if isinstance(root, dict):
            # actual root; can still be merged into a different once, but only
            # one with a proper root as well.
            return renaming.get(name, name)
        if isinstance(root, str):
            return final_root_reference(root)
        fail("Invalid root found for %r: %r" % (name, root))

    new_repos: Json = {}
    for name in repos["repositories"].keys():
        if name not in renaming:
            desc = repos["repositories"][name]
            if name in updated_repos:
                desc = dict(desc, **{"repository": updated_repos[name]})
            if "bindings" in desc:
                bindings = desc["bindings"]
                new_bindings = {}
                for k, v in bindings.items():
                    if v in renaming:
                        new_bindings[k] = renaming[v]
                    else:
                        new_bindings[k] = v
                desc = dict(desc, **{"bindings": new_bindings})
            new_roots: Json = {}
            for root in ["repository", "target_root", "rule_root"]:
                root_val: Json = desc.get(root)
                if isinstance(root_val, str) and (root_val in renaming):
                    new_roots[root] = final_root_reference(root_val)
            desc = dict(desc, **new_roots)

            # Update target repos of precomputed roots:
            if isinstance(desc.get("repository"), dict):
                repo_root: Json = desc.get("repository")
                if repo_root["type"] in ["computed", "tree structure"] and \
                    repo_root["repo"] in renaming:
                    repo_root = \
                        dict(repo_root, **{"repo": renaming[repo_root["repo"]]})
                    desc = dict(desc, **{"repository": repo_root})
            new_repos[name] = desc
    return dict(repos, **{"repositories": new_repos})


if __name__ == "__main__":
    orig = json.load(sys.stdin)
    final = dedup(orig, sys.argv[1:])
    print(json.dumps(final))
