#!/usr/bin/env python3
# Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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
import subprocess
import shutil
import sys
import tempfile

from argparse import ArgumentParser
from pathlib import Path


def log(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)

def fail(s, exit_code=1):
    log(f"Error: {s}")
    sys.exit(exit_code)

MARKERS = [".git", "ROOT", "WORKSPACE"]
SYSTEM_ROOT = os.path.abspath(os.sep)
DEFAULT_CONFIG_LOCATIONS = [{
    "root": "workspace",
    "path": "repos.json"
}, {
    "root": "workspace",
    "path": "etc/repos.json"
}, {
    "root": "home",
    "path": ".just-repos.json"
}, {
    "root": "system",
    "path": "etc/just-repos.json"
}]

def run_cmd(cmd,
            *,
            env=None,
            stdout=subprocess.DEVNULL,
            stdin=None,
            cwd):
    result = subprocess.run(cmd,
                            cwd=cwd,
                            env=env,
                            stdout=stdout,
                            stdin=stdin)
    if result.returncode != 0:
        fail("Command %s in %s failed" % (cmd, cwd))
    return result.stdout

def find_workspace_root(path=None):
    def is_workspace_root(path):
        for m in MARKERS:
            if os.path.exists(os.path.join(path, m)):
                return True
        return False

    if not path:
        path = os.getcwd()
    while True:
        if is_workspace_root(path):
            return path
        if path == SYSTEM_ROOT:
            return None
        path = os.path.dirname(path)


def read_location(location, root=None):
    search_root = location.get("root", None)
    search_path = location.get("path", None)

    fs_root = None
    if search_root == "workspace":
        if root:
            fs_root = root
        else:
            fs_root = find_workspace_root()
    if not root:
        if search_root == "home":
            fs_root = Path.home()
        if search_root == "system":
            fs_root = SYSTEM_ROOT

    if fs_root:
        return os.path.realpath(os.path.join(fs_root, search_path))
    return "/" # certainly not a file

def get_repository_config_file(root=None):
    for location in DEFAULT_CONFIG_LOCATIONS:
        path = read_location(location, root=root)
        if path and os.path.isfile(path):
            return path


def get_base_config(repository_config):
    if not repository_config:
        repository_config = get_repository_config_file()
    with open(repository_config) as f:
        return json.load(f)

def clone(url, branch):
    # clone the given git repository, checkout the specified
    # branch, and return the checkout location
    workdir = tempfile.mkdtemp()
    run_cmd(["git", "clone", url, "src"],
            cwd=workdir)
    srcdir = os.path.join(workdir, "src")
    run_cmd(["git", "checkout", branch],
            cwd = srcdir)
    commit = run_cmd(["git", "log", "-n", "1", "--pretty=%H"],
                     cwd = srcdir, stdout=subprocess.PIPE).decode('utf-8').strip()
    log("Importing commit %s" % (commit,))
    repo = { "type": "git",
             "repository": url,
             "branch": branch,
             "commit": commit,
            }
    return srcdir, repo, workdir

def get_repo_to_import(config):
    """From a given repository config, take the main repository."""
    if config.get("main") is not None:
        return config.get("main")
    repos = config.get("repositories", {}).keys()
    if repos:
        return sorted(repos)[0]
    fail("Config does not contain any repositories; unsure what to import")

def repos_to_import(repos_config, entry, known=None):
    """Compute the set of transitively reachable repositories"""
    visited = set()
    to_import = []
    if known:
        visited = set(known)

    def visit(name):
        if name in visited:
            return
        to_import.append(name)
        visited.add(name)
        for n in repos_config.get(name, {}).get("bindings", {}).values():
            visit(n)

    visit(entry)
    return to_import

def extra_layers_to_import(repos_config, repos):
    """Compute the collection of repositories additionally needed as they serve
    as layers for the repositories to import."""
    extra_imports = set()
    for repo in repos:
        for layer in ["target_root", "rule_root", "expression_root"]:
            if layer in repos_config[repo]:
                extra = repos_config[repo][layer]
                if (extra not in repos) and (extra not in extra_imports):
                    extra_imports.add(extra)
    return list(extra_imports)

def name_imports(to_import, existing, base_name, main=None):
    """Assign names to the repositories to import in such a way that
    no conflicts arise."""
    assign = {}

    def find_name(name):
        base = "%s/%s" % (base_name, name)
        if (base not in existing) and (base not in assign):
            return base
        count = 0
        while True:
            count += 1
            candidate = base + " (%d)" % count
            if (candidate not in existing) and (candidate not in assign):
                return candidate

    if main is not None and (base_name not in existing):
        assign[main] = base_name
        to_import = [x for x in to_import if x != main]
    for repo in to_import:
        assign[repo] = find_name(repo)
    return assign

def rewrite_repo(repo_spec, *, remote, assign):
    new_spec = {}
    repo = repo_spec.get("repository", {})
    if isinstance(repo, str):
        repo = assign[repo]
    elif repo.get("type") == "file":
        changes = {}
        subdir = repo.get("path", ".")
        if subdir not in ["", "."]:
            changes["subdir"] = subdir
        repo = dict(remote, **changes)
    elif repo.get("type") == "distdir":
        new_repos = [assign[k] for k in repo.get("repositories", [])]
        repo = dict(repo, **{"repositories": new_repos})
    new_spec["repository"] = repo
    for key in ["target_root", "rule_root", "expression_root"]:
        if key in repo_spec:
            new_spec[key] = assign[repo_spec[key]]
    for key in ["target_file_name", "rule_file_name", "expression_file_name"]:
        if key in repo_spec:
            new_spec[key] = repo_spec[key]
    bindings = repo_spec.get("bindings", {})
    new_bindings = {}
    for k, v in bindings.items():
        new_bindings[k] = assign[v]
    if new_bindings:
        new_spec["bindings"] = new_bindings
    return new_spec

def handle_import(args):
    base_config = get_base_config(args.repository_config)
    base_repos = base_config.get("repositories", {})
    srcdir, remote, to_cleanup = clone(args.URL, args.branch)
    if args.foreign_repository_config:
        foreign_config_file = args.foreign_repository_config
    else:
        foreign_config_file = get_repository_config_file(srcdir)
    with open(foreign_config_file) as f:
        foreign_config = json.load(f)
    foreign_repos = foreign_config.get("repositories", {})
    if args.foreign_repository_name:
        foreign_name = args.foreign_repository_name
    else:
        foreign_name = get_repo_to_import(foreign_config)
    import_map = {}
    for theirs, ours in args.import_map:
        import_map[theirs] = ours
    main_repos = repos_to_import(foreign_repos, foreign_name, import_map.keys())
    extra_repos = sorted([x for x in main_repos if x != foreign_name])
    extra_imports = sorted(extra_layers_to_import(foreign_repos, main_repos))
    ordered_imports = [foreign_name] + extra_repos + extra_imports
    import_name = foreign_name
    if args.import_as is not None:
        import_name = args.import_as
    assign = name_imports(
        ordered_imports,
        set(base_repos.keys()),
        import_name,
        main = foreign_name,
    )
    log("Importing %r as %r" % (foreign_name, import_name))
    log("Transitive dependencies to import: %r" % (extra_repos,))
    log("Repositories imported as layers: %r" % (extra_imports,))
    total_assign = dict(assign, **import_map)
    for repo in ordered_imports:
        base_repos[assign[repo]] = rewrite_repo(
            foreign_repos[repo],
            remote = remote,
            assign = total_assign,
        )
    base_config["repositories"] = base_repos
    shutil.rmtree(to_cleanup)
    return base_config

def main():
    parser = ArgumentParser(
        prog = "just-import-deps",
        description =
        "Import a dependency transitively into a given"
        + " multi-repository configuration"
    )
    parser.add_argument(
        "-C",
        dest="repository_config",
        help="Repository-description file to import into",
        metavar="FILE"
    )
    parser.add_argument(
        "-b",
        dest="branch",
        help="The branch of the remote repository to import and follow",
        metavar="branch",
        default="master"
    )
    parser.add_argument(
        "-R",
        dest="foreign_repository_config",
        help="Repository-description file in the repository to import",
        metavar="relative-path"
    )
    parser.add_argument(
        "--as",
        dest="import_as",
        help="Name prefix to import the foreign repository as",
        metavar="NAME",
    )
    parser.add_argument(
        "--map",
        nargs=2,
        dest="import_map",
        help="Map the specified foreign repository to the specified existing repository",
        action="append",
        default=[]
    )
    parser.add_argument('URL')
    parser.add_argument('foreign_repository_name', nargs='?')
    args = parser.parse_args()
    new_config = handle_import(args)
    print(json.dumps(new_config))


if __name__ == "__main__":
    main()
