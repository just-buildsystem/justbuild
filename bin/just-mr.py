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

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Optional

from argparse import ArgumentParser
from pathlib import Path

JUST = "just"
JUST_ARGS = {}
ROOT = "/justroot"
SYSTEM_ROOT = os.path.abspath(os.sep)
WORKSPACE_ROOT = None
SETUP_ROOT = None
DISTDIR = []
MARKERS = [".git", "ROOT", "WORKSPACE"]

ALWAYS_FILE = False

GIT_CHECKOUT_LOCATIONS_FILE = None
GIT_CHECKOUT_LOCATIONS = {}

TAKE_OVER = [
    "bindings",
    "target_file_name",
    "rule_file_name",
    "expression_file_name",
]
ALT_DIRS = [
    "target_root",
    "rule_root",
    "expression_root",
]

GIT_NOBODY_ENV = {
    "GIT_AUTHOR_DATE": "1970-01-01T00:00Z",
    "GIT_AUTHOR_NAME": "Nobody",
    "GIT_AUTHOR_EMAIL": "nobody@example.org",
    "GIT_COMMITTER_DATE": "1970-01-01T00:00Z",
    "GIT_COMMITTER_NAME": "Nobody",
    "GIT_COMMITTER_EMAIL": "nobody@example.org",
    "GIT_CONFIG_GLOBAL": "/dev/null",
    "GIT_CONFIG_SYSTEM": "/dev/null",
}

KNOWN_JUST_SUBCOMMANDS = {
    "version": {
        "config": False,
        "build root": False
    },
    "describe": {
        "config": True,
        "build root": False
    },
    "analyse": {
        "config": True,
        "build root": True
    },
    "build": {
        "config": True,
        "build root": True
    },
    "install": {
        "config": True,
        "build root": True
    },
    "rebuild": {
        "config": True,
        "build root": True
    },
    "install-cas": {
        "config": False,
        "build root": True
    },
    "gc": {
        "config": False,
        "build root": True
    }
}
CURRENT_SUBCOMMAND = None

LOCATION_TYPES = ["workspace", "home", "system"]

DEFAULT_RC_PATH = os.path.join(Path.home(), ".just-mrrc")
DEFAULT_BUILD_ROOT = os.path.join(Path.home(), ".cache/just")
DEFAULT_CHECKOUT_LOCATIONS_FILE = os.path.join(Path.home(), ".just-local.json")
DEFAULT_DISTDIRS = [os.path.join(Path.home(), ".distfiles")]
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


def log(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)


def fail(s, exit_code=65):
    log(f"Error: {s}")
    sys.exit(exit_code)


def try_rmtree(tree):
    for _ in range(10):
        try:
            shutil.rmtree(tree)
            return
        except:
            time.sleep(1.0)
    fail("Failed to remove %s" % (tree, ))


def move_to_place(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    try:
        os.rename(src, dst)
    except Exception as e:
        if not os.path.exists(dst):
            fail("Failed to move %s to %s: %s" % (src, dst, e))
    if not os.path.exists(dst):
        fail("%s not present after move" % (dst, ))


def run_cmd(cmd,
            *,
            env=None,
            stdout=subprocess.DEVNULL,
            stdin=None,
            cwd,
            attempts=1):
    for _ in range(attempts):
        result = subprocess.run(cmd,
                                cwd=cwd,
                                env=env,
                                stdout=stdout,
                                stdin=stdin)
        if result.returncode == 0:
            return
        time.sleep(1.0)
    if result.returncode != 0:
        fail("Command %s in %s failed" % (cmd, cwd))


def find_workspace_root() -> Optional[str]:
    def is_workspace_root(path: str) -> bool:
        for m in MARKERS:
            if os.path.exists(os.path.join(path, m)):
                return True
        return False

    path = os.getcwd()
    while True:
        if is_workspace_root(path):
            return path
        if path == SYSTEM_ROOT:
            return None
        path = os.path.dirname(path)


def read_config(configfile):
    with open(configfile) as f:
        return json.load(f)


def git_root(*, upstream):
    if upstream in GIT_CHECKOUT_LOCATIONS:
        return GIT_CHECKOUT_LOCATIONS[upstream]
    elif upstream and os.path.isabs(upstream) and os.path.isdir(upstream):
        return upstream
    else:
        return os.path.join(ROOT, "git")


def is_cache_git_root(upstream):
    """return true if upstream is the default git root in the cache directory"""
    return git_root(upstream=upstream) == git_root(upstream=None)


def git_keep(commit, *, upstream):
    if not is_cache_git_root(upstream):
        # for those, we assume the referenced commit is kept by
        # some branch anyway
        return
    run_cmd([
        "git", "tag", "-f", "-m", "Keep referenced tree alive",
        "keep-%s" % (commit, ), commit
    ],
            cwd=git_root(upstream=upstream),
            env=dict(os.environ, **GIT_NOBODY_ENV),
            attempts=3)


def git_init_options(*, upstream):
    if upstream in GIT_CHECKOUT_LOCATIONS:
        return []
    else:
        return ["--bare"]


def ensure_git(*, upstream):
    root = git_root(upstream=upstream)
    if os.path.exists(root):
        return
    os.makedirs(root)
    run_cmd(["git", "init"] + git_init_options(upstream=upstream), cwd=root)


def git_commit_present(commit, *, upstream):
    result = subprocess.run(["git", "show", "--oneline", commit],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL,
                            cwd=git_root(upstream=upstream))
    return result.returncode == 0


def git_url_is_path(url):
    for prefix in ["ssh://", "http://", "https://"]:
        if url.startswith(prefix):
            return False
    return True


def git_fetch(*, repo, branch):
    if git_url_is_path(repo):
        repo = os.path.abspath(repo)
    run_cmd(["git", "fetch", repo, branch], cwd=git_root(upstream=repo))


def subdir_path(checkout, desc):
    return os.path.normpath(os.path.join(checkout, desc.get("subdir", ".")))


def git_tree(*, commit, subdir, upstream):
    tree = subprocess.run(
        ["git", "log", "-n", "1", "--format=%T", commit],
        stdout=subprocess.PIPE,
        cwd=git_root(upstream=upstream)).stdout.decode('utf-8').strip()
    return git_subtree(tree=tree, subdir=subdir, upstream=upstream)


def git_subtree(*, tree, subdir, upstream):
    if subdir == ".":
        return tree
    return subprocess.Popen(
        ["git", "cat-file", "--batch-check=%(objectname)"],
        stdout=subprocess.PIPE,
        stdin=subprocess.PIPE,
        cwd=git_root(upstream=upstream)).communicate(
            input=("%s:%s" %
                   (tree, subdir)).encode())[0].decode('utf-8').strip()


def git_checkout_dir(commit):
    return os.path.join(ROOT, "workspaces", "git", commit)


def git_checkout(desc):
    commit = desc["commit"]
    target = git_checkout_dir(commit)
    if ALWAYS_FILE and os.path.exists(target):
        return ["file", subdir_path(target, desc)]
    repo = desc["repository"]
    if git_url_is_path(repo):
        repo = os.path.abspath(repo)
    root = git_root(upstream=repo)
    ensure_git(upstream=repo)
    if not git_commit_present(commit, upstream=repo):
        branch = desc["branch"]
        log("Fetching %s from %s (in %s)" % (branch, repo, root))
        git_fetch(repo=repo, branch=branch)
        if not git_commit_present(commit, upstream=repo):
            fail("Fetching %s from %s failed to fetch %s" %
                 (branch, repo, commit))
        git_keep(commit, upstream=repo)
    if ALWAYS_FILE:
        if os.path.exists(target):
            try_rmtree(target)
        os.makedirs(target)
        with tempfile.TemporaryFile() as f:
            run_cmd(["git", "archive", commit], cwd=root, stdout=f)
            f.seek(0)
            run_cmd(["tar", "x"], cwd=target, stdin=f)
            return ["file", subdir_path(target, desc)]
    tree = git_tree(commit=commit,
                    subdir=desc.get("subdir", "."),
                    upstream=repo)
    return ["git tree", tree, root]


def update_git(desc):
    repo = desc["repository"]
    branch = desc["branch"]
    lsremote = subprocess.run(["git", "ls-remote", repo, branch],
                              stdout=subprocess.PIPE).stdout
    desc["commit"] = lsremote.decode('utf-8').split('\t')[0]


def git_hash(content):
    header = "blob {}\0".format(len(content)).encode('utf-8')
    h = hashlib.sha1()
    h.update(header)
    h.update(content)
    return h.hexdigest()


def add_to_cas(data):
    if isinstance(data, str):
        data = data.encode('utf-8')
    h = git_hash(data)
    cas_root = os.path.join(
        ROOT, f"protocol-dependent/generation-0/git-sha1/casf/{h[0:2]}")
    basename = h[2:]
    target = os.path.join(cas_root, basename)
    tempname = os.path.join(cas_root, "%s.%d" % (basename, os.getpid()))

    if os.path.exists(target):
        return target

    os.makedirs(cas_root, exist_ok=True)
    with open(tempname, "wb") as f:
        f.write(data)
        f.flush()
        os.chmod(f.fileno(), 0o444)
        os.fsync(f.fileno())
    os.utime(tempname, (0, 0))
    os.rename(tempname, target)
    return target


def cas_path(h):
    return os.path.join(
        ROOT, f"protocol-dependent/generation-0/git-sha1/casf/{h[0:2]}", h[2:])


def is_in_cas(h):
    return os.path.exists(cas_path(h))


def add_file_to_cas(filename):
    # TODO: avoid going through memory
    with open(filename, "rb") as f:
        data = f.read()
    add_to_cas(data)


def add_distfile_to_cas(distfile):
    for d in DISTDIR:
        candidate = os.path.join(d, distfile)
        if os.path.exists(candidate):
            add_file_to_cas(candidate)


def archive_checkout_dir(content, repo_type):
    return os.path.join(ROOT, "workspaces", repo_type, content)


def archive_tmp_checkout_dir(content, repo_type):
    return os.path.join(ROOT, "tmp-workspaces", repo_type,
                        "%d-%s" % (os.getpid(), content))


def archive_tree_id_file(content, repo_type):
    return os.path.join(ROOT, "tree-map", repo_type, content)


def get_distfile(desc):
    distfile = desc.get("distfile")
    if not distfile:
        distfile = os.path.basename(desc.get("fetch"))
    return distfile


def archive_fetch(desc, content):
    """ Makes sure archive is available and accounted for in cas
    """
    if not is_in_cas(content):
        distfile = get_distfile(desc)
        if distfile:
            add_distfile_to_cas(distfile)
    if not is_in_cas(content):
        url = desc["fetch"]
        data = subprocess.run(["wget", "-O", "-", url],
                              stdout=subprocess.PIPE).stdout
        if "sha256" in desc:
            actual_hash = hashlib.sha256(data).hexdigest()
            if desc["sha256"] != actual_hash:
                fail("SHA256 mismatch for %s, expected %s, found %s" %
                     (url, desc["sha256"], actual_hash))
        if "sha512" in desc:
            actual_hash = hashlib.sha512(data).hexdigest()
            if desc["sha512"] != actual_hash:
                fail("SHA512 mismatch for %s, expected %s, found %s" %
                     (url, desc["sha512"], actual_hash))
        add_to_cas(data)
        if not is_in_cas(content):
            fail("Failed to fetch a file with id %s from %s" % (content, url))


def archive_checkout(desc, repo_type="archive"):
    content_id = desc["content"]
    target = archive_checkout_dir(content_id, repo_type=repo_type)
    if ALWAYS_FILE and os.path.exists(target):
        return ["file", subdir_path(target, desc)]
    tree_id_file = archive_tree_id_file(content_id, repo_type=repo_type)
    if (not ALWAYS_FILE) and os.path.exists(tree_id_file):
        with open(tree_id_file) as f:
            archive_tree_id = f.read()
        # ensure git cache exists
        ensure_git(upstream=None)
        return [
            "git tree",
            git_subtree(tree=archive_tree_id,
                        subdir=desc.get("subdir", "."),
                        upstream=None),
            git_root(upstream=None),
        ]
    archive_fetch(desc, content=content_id)
    target_tmp = archive_tmp_checkout_dir(content_id, repo_type=repo_type)
    if os.path.exists(target_tmp):
        try_rmtree(target_tmp)
    os.makedirs(target_tmp)
    if repo_type == "zip":
        try:
            run_cmd(["unzip", "-d", ".", cas_path(content_id)], cwd=target_tmp)
        except:
            try:
                run_cmd(["7z", "x", cas_path(content_id)], cwd=target_tmp)
            except:
                print("Failed to extract zip-like archive %s" %
                      (cas_path(content_id), ))
                sys.exit(1)
    else:
        try:
            run_cmd(["tar", "xf", cas_path(content_id)], cwd=target_tmp)
        except:
            print("Failed to extract tarball %s" % (cas_path(content_id), ))
            sys.exit(1)
    if ALWAYS_FILE:
        move_to_place(target_tmp, target)
        return ["file", subdir_path(target, desc)]
    tree = import_to_git(target_tmp, repo_type, content_id)
    try_rmtree(target_tmp)
    os.makedirs(os.path.dirname(tree_id_file), exist_ok=True)
    with open(tree_id_file, "w") as f:
        f.write(tree)
    return [
        "git tree",
        git_subtree(tree=tree, subdir=desc.get("subdir", "."), upstream=None),
        git_root(upstream=None)
    ]


def import_to_git(target, repo_type, content_id):
    run_cmd(
        ["git", "init"],
        cwd=target,
        env=dict(os.environ, **GIT_NOBODY_ENV),
    )
    run_cmd(
        ["git", "add", "-f", "."],
        cwd=target,
        env=dict(os.environ, **GIT_NOBODY_ENV),
    )
    run_cmd(
        ["git", "commit", "-m",
         "Content of %s %r" % (repo_type, content_id)],
        cwd=target,
        env=dict(os.environ, **GIT_NOBODY_ENV),
    )

    ensure_git(upstream=None)
    run_cmd(["git", "fetch", target], cwd=git_root(upstream=None))
    commit = subprocess.run(["git", "log", "-n", "1", "--format=%H"],
                            stdout=subprocess.PIPE,
                            cwd=target).stdout.decode('utf-8').strip()
    git_keep(commit, upstream=None)
    return subprocess.run(["git", "log", "-n", "1", "--format=%T"],
                          stdout=subprocess.PIPE,
                          cwd=target).stdout.decode('utf-8').strip()


def file_as_git(fpath):
    root_result = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                                 cwd=fpath,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.DEVNULL)
    if not root_result.returncode == 0:
        # TODO: consider also doing this for pending changes
        # copy non-git paths to tmp-workspace and import to git
        fpath = os.path.realpath(fpath)
        if CURRENT_SUBCOMMAND:
            log(f"""\
Warning: Inefficient Git import of file path '{fpath}'.
         Please consider using 'just-mr setup' and 'just {CURRENT_SUBCOMMAND}'
         separately to cache the output.""")
        target = archive_tmp_checkout_dir(os.path.relpath(fpath, "/"),
                                          repo_type="file")
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copytree(fpath, target)
        tree = import_to_git(target, "file", fpath)
        try_rmtree(target)
        return ["git tree", tree, git_root(upstream=None)]
    root = root_result.stdout.decode('utf-8').rstrip()
    subdir = os.path.relpath(fpath, root)
    root_tree = subprocess.run(
        ["git", "log", "-n", "1", "--format=%T"],
        cwd=root,
        stdout=subprocess.PIPE).stdout.decode('utf-8').strip()
    return [
        "git tree",
        git_subtree(tree=root_tree, subdir=subdir, upstream=root), root
    ]


def file_checkout(desc):
    fpath = os.path.abspath(desc["path"])
    if desc.get("pragma", {}).get("to_git") and not ALWAYS_FILE:
        return file_as_git(fpath)
    return ["file", os.path.abspath(fpath)]


def resolve_repo(desc, *, seen=None, repos):
    seen = seen or []
    if not isinstance(desc, str):
        return desc
    if desc in seen:
        fail("Cyclic reference in repository source definition: %r" % (seen, ))
    return resolve_repo(repos[desc]["repository"],
                        seen=seen + [desc],
                        repos=repos)


def distdir_repo_dir(content):
    return os.path.join(ROOT, "distdir", content)


def distdir_tmp_dir(content):
    return os.path.join(ROOT, "tmp-workspaces", "distdir",
                        "%d-%s" % (os.getpid(), content))


def distdir_tree_id_file(content):
    return os.path.join(ROOT, "distfiles-tree-map", content)


def distdir_checkout(desc, repos):
    """ Logic for processing the distdir repo type.
    """
    content = {}

    # All repos in distdir list are considered, irrespective of reachability
    distdir_repos = desc.get("repositories", [])
    for repo in distdir_repos:
        # If repo does not exist, fail
        if repo not in repos:
            print("No configuration for repository %s found" % (repo, ))
            sys.exit(1)
        repo_desc = repos[repo].get("repository", {})
        repo_desc_type = repo_desc.get("type")
        # Only do work for archived types
        if repo_desc_type in ["archive", "zip"]:
            # fetch repo
            content_id = repo_desc["content"]
            # Store the relevant info in the map
            content[get_distfile(repo_desc)] = content_id

    # Hash the map as unique id for the distdir repo entry
    distdir_content_id = git_hash(
        json.dumps(content, sort_keys=True,
                   separators=(',', ':')).encode('utf-8'))
    target_distdir_dir = distdir_repo_dir(distdir_content_id)

    # Check if content already exists
    if ALWAYS_FILE and os.path.exists(target_distdir_dir):
        return ["file", target_distdir_dir]
    tree_id_file = distdir_tree_id_file(distdir_content_id)
    if (not ALWAYS_FILE) and os.path.exists(tree_id_file):
        with open(tree_id_file) as f:
            distdir_tree_id = f.read()
        # ensure git cache exists
        ensure_git(upstream=None)
        return [
            "git tree",
            git_subtree(tree=distdir_tree_id, subdir=".", upstream=None),
            git_root(upstream=None)
        ]

    # As the content is not there already, so we have to ensure the archives
    # are present.
    for repo in distdir_repos:
        repo_desc = repos[repo].get("repository", {})
        repo_desc_type = repo_desc.get("type")
        if repo_desc_type in ["archive", "zip"]:
            content_id = repo_desc["content"]
            archive_fetch(repo_desc, content=content_id)

    # Create the dirstdir repo folder content
    target_tmp_dir = distdir_tmp_dir(distdir_content_id)
    if os.path.exists(target_tmp_dir):
        try_rmtree(target_tmp_dir)
    os.makedirs(target_tmp_dir)
    for name, content_id in content.items():
        target = os.path.join(target_tmp_dir, name)
        os.link(cas_path(content_id), target)

    if (ALWAYS_FILE):
        # Return with path to distdir repo
        move_to_place(target_tmp_dir, target_distdir_dir)
        return ["file", target_distdir_dir]

    # Gitify the distdir repo folder
    tree = import_to_git(target_tmp_dir, "distdir", distdir_content_id)
    try_rmtree(target_tmp_dir)

    # Cache git info to tree id file
    os.makedirs(os.path.dirname(tree_id_file), exist_ok=True)
    with open(tree_id_file, "w") as f:
        f.write(tree)

    # Return git tree info
    return [
        "git tree",
        git_subtree(tree=tree, subdir=".", upstream=None),
        git_root(upstream=None)
    ]


def checkout(desc, *, name, repos):
    repo_desc = resolve_repo(desc, repos=repos)
    repo_type = repo_desc.get("type")
    if repo_type == "git":
        return git_checkout(repo_desc)
    if repo_type in ["archive", "zip"]:
        return archive_checkout(repo_desc, repo_type=repo_type)
    if repo_type == "file":
        return file_checkout(repo_desc)
    if repo_type == "distdir":
        return distdir_checkout(repo_desc, repos=repos)
    fail("Unknown repository type %s for %s" % (repo_type, name))


def reachable_repositories(repo, *, repos):
    # First compute the set of repositories transitively reachable via bindings
    reachable = set()

    def traverse(x):
        nonlocal reachable
        if x in reachable:
            return
        reachable.add(x)
        bindings = repos[x].get("bindings", {})
        for bound in bindings.values():
            traverse(bound)

    traverse(repo)

    # Now add the repositories that serve as overlay directories for
    # targets, rules, etc. Those repositories have to be fetched as well, but
    # we do not have to consider their bindings.
    to_fetch = reachable.copy()
    for x in reachable:
        for layer in ALT_DIRS:
            if layer in repos[x]:
                to_fetch.add(repos[x][layer])

    return reachable, to_fetch


def setup(*, config, main, setup_all=False, interactive=False):
    if SETUP_ROOT:
        cwd = os.getcwd()
        os.chdir(SETUP_ROOT)
    repos = config.get("repositories", {})
    repos_to_setup = repos.keys()
    repos_to_include = repos.keys()
    mr_config = {}

    if main == None:
        main = config.setdefault("main", None)
        if not isinstance(main, str) and main != None:
            fail("Unsupported value for field 'main' in configuration.")

    if main != None:
        # pass on main that was explicitly set via command line or config
        mr_config["main"] = main

    if main == None and len(repos_to_setup) > 0:
        main = sorted(list(repos_to_setup))[0]

    if main != None and not setup_all:
        repos_to_include, repos_to_setup = reachable_repositories(main,
                                                                  repos=repos)

    mr_repos = {}
    for repo in repos_to_setup:
        desc = repos[repo]
        if repo == main and interactive:
            config = {}
        else:
            workspace = checkout(desc.get("repository", {}),
                                 name=repo,
                                 repos=repos)
            config = {"workspace_root": workspace}
        for key in TAKE_OVER:
            val = desc.get(key, {})
            if val:
                config[key] = val
        mr_repos[repo] = config
    # Alternate directories are specifies as the workspace of
    # some other repository. So we have to iterate over all repositories again
    # to add those directories. We do this only for the repositories we include
    # in the final configuration.
    for repo in repos_to_include:
        desc = repos[repo]
        if repo == main and interactive:
            continue
        for key in ALT_DIRS:
            val = desc.get(key, {})
            if val:
                if val == main and interactive:
                    continue
                mr_repos[repo][key] = mr_repos[val]["workspace_root"]
    mr_repos_actual = {}
    for repo in repos_to_include:
        mr_repos_actual[repo] = mr_repos[repo]
    mr_config["repositories"] = mr_repos_actual

    if SETUP_ROOT:
        os.chdir(cwd)

    return add_to_cas(json.dumps(mr_config, indent=2, sort_keys=True))


def call_just(*, config, main, args):
    subcommand = args[0] if len(args) > 0 else None
    args = args[1:]
    use_config = False
    use_build_root = False
    if subcommand in KNOWN_JUST_SUBCOMMANDS:
        if KNOWN_JUST_SUBCOMMANDS[subcommand]["config"]:
            use_config = True
            global CURRENT_SUBCOMMAND
            CURRENT_SUBCOMMAND = subcommand
            config = setup(config=config, main=main)
            CURRENT_SUBCOMMAND = None
        use_build_root = KNOWN_JUST_SUBCOMMANDS[subcommand]["build root"]
    cmd = [JUST]
    cmd += [subcommand] if subcommand else []
    cmd += ["-C", config] if use_config else []
    cmd += ["--local-build-root", ROOT] if use_build_root else []
    if subcommand in JUST_ARGS:
        cmd += JUST_ARGS[subcommand]
    cmd += args
    log("Setup finished, exec %s" % (cmd, ))
    try:
        os.execvp(JUST, cmd)
    except Exception as e:
        log(e)
    finally:
        fail(f"exec failed", 64)


def update(*, config, repos):
    for repo in repos:
        desc = config["repositories"][repo]["repository"]
        desc = resolve_repo(desc, repos=config["repositories"])
        repo_type = desc.get("type")
        if repo_type == "git":
            update_git(desc)
        else:
            fail("Don't know how to update %s repositories" % (repo_type, ))
    print(json.dumps(config, indent=2))
    sys.exit(0)


def fetch(*, config, fetch_dir, main, fetch_all=False):
    if not fetch_dir:
        for d in DISTDIR:
            if os.path.isdir(d):
                fetch_dir = os.path.abspath(d)
                break
    if not fetch_dir:
        fail("No directory found to fetch to, considered %r" % (DISTDIR, ))

    repos = config["repositories"]
    repos_to_fetch = repos.keys()

    if not fetch_all:
        if main == None and len(repos_to_fetch) > 0:
            main = sorted(list(repos_to_fetch))[0]
        if main != None:
            repos_to_fetch = reachable_repositories(main, repos=repos)[0]

        def is_subpath(path, base):
            return os.path.commonpath([path, base]) == base

        # warn if fetch_dir is in invocation workspace
        if WORKSPACE_ROOT and is_subpath(fetch_dir, WORKSPACE_ROOT):
            repo = repos.get(main, {}).get("repository", {})
            repo_path = repo.get("path", None)
            if repo_path != None and repo.get("type", None) == "file":
                if not os.path.isabs(repo_path):
                    repo_path = os.path.realpath(
                        os.path.join(SETUP_ROOT, repo_path))
                # only warn if repo workspace differs to invocation workspace
                if not is_subpath(repo_path, WORKSPACE_ROOT):
                    log(f"""\
Warning: Writing distribution files to workspace location '{fetch_dir}',
         which is different to the workspace of the requested main repository
         '{repo_path}'.""")

    print("Fetching to %r" % (fetch_dir, ))

    for repo in repos_to_fetch:
        desc = repos[repo]
        if ("repository" in desc and isinstance(desc["repository"], dict)
                and desc["repository"]["type"] in ["zip", "archive"]):
            repo_desc = desc["repository"]
            distfile = repo_desc.get("distfile") or os.path.basename(
                repo_desc["fetch"])
            content = repo_desc["content"]
            print("%r --> %r (content: %s)" % (repo, distfile, content))
            archive_fetch(repo_desc, content)
            shutil.copyfile(cas_path(content),
                            os.path.join(fetch_dir, distfile))

    sys.exit(0)


def read_location(location):
    root = location.setdefault("root", None)
    path = location.setdefault("path", None)
    base = location.setdefault("base", ".")

    if not path or root not in LOCATION_TYPES:
        fail(f"malformed location object: {location}")

    if root == "workspace":
        if not WORKSPACE_ROOT:
            log(f"Warning: Not in workspace root, ignoring location %s." %
                (location, ))
            return None
        root = WORKSPACE_ROOT
    if root == "home":
        root = Path.home()
    if root == "system":
        root = SYSTEM_ROOT

    return os.path.realpath(os.path.join(root, path)), os.path.realpath(
        os.path.join(root, base))


# read settings from just-mrrc and return config path
def read_justmrrc(rcpath, no_rc=False):
    rc = {}
    if not no_rc:
        if not rcpath:
            rcpath = DEFAULT_RC_PATH
        elif not os.path.isfile(rcpath):
            fail(f"cannot read rc file {rcpath}.")
        if os.path.isfile(rcpath):
            with open(rcpath) as f:
                rc = json.load(f)

    location = rc.get("local build root", None)
    build_root = read_location(location) if location else None
    if build_root:
        global ROOT
        ROOT = build_root[0]

    location = rc.get("checkout locations", None)
    checkout = read_location(location) if location else None
    if checkout:
        global GIT_CHECKOUT_LOCATIONS_FILE
        if not os.path.isfile(checkout[0]):
            fail(f"cannot find checkout locations files {checkout[0]}.")
        GIT_CHECKOUT_LOCATIONS_FILE = checkout[0]

    location = rc.get("distdirs", None)
    if location:
        global DISTDIR
        DISTDIR = []
        for l in location:
            paths = read_location(l)
            if paths:
                if os.path.isdir(paths[0]):
                    DISTDIR.append(paths[0])
                else:
                    log(f"Warning: Ignoring non-existing distdir {paths[0]}.")

    location = rc.get("just", None)
    just = read_location(location) if location else None
    if just:
        global JUST
        JUST = just[0]

    global JUST_ARGS
    JUST_ARGS = rc.get("just args", {})

    for location in rc.get("config lookup order", DEFAULT_CONFIG_LOCATIONS):
        paths = read_location(location)
        if paths and os.path.isfile(paths[0]):
            global SETUP_ROOT
            SETUP_ROOT = paths[1]
            return paths[0]

    return None


def main():
    parser = ArgumentParser()
    parser.add_argument("-C",
                        dest="repository_config",
                        help="Repository-description file to use",
                        metavar="FILE")
    parser.add_argument("--checkout-locations",
                        dest="checkout_location",
                        help="Specification file for checkout locations")
    parser.add_argument("--local-build-root",
                        dest="local_build_root",
                        help="Root for CAS, repository space, etc",
                        metavar="PATH")
    parser.add_argument("--distdir",
                        dest="distdir",
                        action="append",
                        default=[],
                        help="Directory to look for distfiles before fetching",
                        metavar="PATH")
    parser.add_argument("--just",
                        dest="just",
                        help="Path to the just binary",
                        metavar="PATH")
    parser.add_argument("--always-file",
                        dest="always_file",
                        action="store_true",
                        default=False,
                        help="Always create file roots")
    parser.add_argument(
        "--main",
        dest="main",
        default=None,
        help="Main repository to consider from the configuration.")
    parser.add_argument("--rc",
                        dest="rcfile",
                        help="Use just-mrrc file from custom path.")
    parser.add_argument("--norc",
                        dest="norc",
                        action="store_true",
                        default=False,
                        help="Do not use any just-mrrc file.")
    subcommands = parser.add_subparsers(dest="subcommand",
                                        title="subcommands",
                                        required=True)

    repo_parser = ArgumentParser(add_help=False)
    repo_parser.add_argument(
        "--all",
        dest="sub_all",
        action="store_true",
        default=False,
        help="Consider all repositories in the configuration.")
    repo_parser.add_argument(
        "sub_main",
        metavar="main-repo",
        nargs="?",
        default=None,
        help="Main repository to consider from the configuration.")

    subcommands.add_parser("setup",
                           parents=[repo_parser],
                           help="Setup and generate just configuration.")

    subcommands.add_parser(
        "setup-env",
        parents=[repo_parser],
        help="Setup without workspace root for the main repository.")

    fetch_parser = subcommands.add_parser(
        "fetch",
        parents=[repo_parser],
        help="Fetch and store distribution files.")
    fetch_parser.add_argument("-o",
                              dest="fetch_dir",
                              help="Directory to write distfiles when fetching",
                              metavar="PATH")

    update_parser = subcommands.add_parser(
        "update",
        help="Advance Git commit IDs and print updated just-mr configuration.")
    update_parser.add_argument("update_repos",
                               metavar="repo",
                               nargs="*",
                               default=[],
                               help="Repository to update.")

    subcommands.add_parser("do",
                           help="Canonical way of specifying just subcommands",
                           add_help=False)

    for cmd in KNOWN_JUST_SUBCOMMANDS:
        subcommands.add_parser(cmd,
                               help=f"Run setup and call 'just {cmd}'",
                               add_help=False)

    (options, args) = parser.parse_known_args()

    global ROOT, GIT_CHECKOUT_LOCATIONS_FILE, DISTDIR, SETUP_ROOT, WORKSPACE_ROOT
    ROOT = DEFAULT_BUILD_ROOT
    if os.path.isfile(DEFAULT_CHECKOUT_LOCATIONS_FILE):
        GIT_CHECKOUT_LOCATIONS_FILE = DEFAULT_CHECKOUT_LOCATIONS_FILE
    DISTDIR = DEFAULT_DISTDIRS
    SETUP_ROOT = os.path.curdir
    WORKSPACE_ROOT = find_workspace_root()

    config_file = read_justmrrc(options.rcfile, options.norc)
    if options.repository_config:
        config_file = options.repository_config

    if not config_file:
        fail("cannot find repository configuration.")
    config = read_config(config_file)

    if options.local_build_root:
        ROOT = os.path.abspath(options.local_build_root)

    if options.checkout_location:
        if not os.path.isfile(options.checkout_location):
            fail("cannot find checkout locations file %s" %
                 (options.checkout_location, ))
        GIT_CHECKOUT_LOCATIONS_FILE = os.path.abspath(options.checkout_location)

    if GIT_CHECKOUT_LOCATIONS_FILE:
        with open(GIT_CHECKOUT_LOCATIONS_FILE) as f:
            global GIT_CHECKOUT_LOCATIONS
            GIT_CHECKOUT_LOCATIONS = json.load(f).get("checkouts",
                                                      {}).get("git", {})

    if options.distdir:
        DISTDIR += options.distdir

    global JUST
    if options.just:
        JUST = options.just
    global ALWAYS_FILE
    ALWAYS_FILE = options.always_file

    sub_main = options.sub_main if hasattr(options, "sub_main") else None
    sub_all = options.sub_all if hasattr(options, "sub_all") else None
    if not sub_all and options.main and sub_main and options.main != sub_main:
        print(
            "Warning: conflicting options for main repository, selecting '%s'."
            % (sub_main, ))
    main = sub_main or options.main

    if options.subcommand in KNOWN_JUST_SUBCOMMANDS:
        call_just(config=config, main=main, args=[options.subcommand] + args)
        return
    if options.subcommand == "do":
        call_just(config=config, main=main, args=args)
        return

    if args:
        print("Warning: ignoring unknown arguments %r" % (args, ))

    if options.subcommand == "setup-env":
        # Setup for interactive use, i.e., fetch the required repositories
        # and generate an appropriate multi-repository configuration file.
        # Store it in the CAS and print its path on stdout.
        #
        # For the main repository (if specified), leave out the workspace
        # so that in the usage of just the workspace is determined from
        # the working directory; in this way, working on a checkout of that
        # repository is possible, while having all dependencies set up
        # correctly.
        print(
            setup(config=config,
                  main=main,
                  setup_all=options.sub_all,
                  interactive=True))
        return
    if options.subcommand == "setup":
        # Setup such that the main repository keeps its workspace given in
        # the input config.
        print(setup(config=config, main=main, setup_all=options.sub_all))
        return
    if options.subcommand == "update":
        update(config=config, repos=options.update_repos)
    if options.subcommand == "fetch":
        fetch(config=config,
              fetch_dir=options.fetch_dir,
              main=main,
              fetch_all=options.sub_all)
    fail("Unknown subcommand %s" % (options.subcommand, ))


if __name__ == "__main__":
    main()
