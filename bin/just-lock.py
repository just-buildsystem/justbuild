#!/usr/bin/env python3
# Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

import fcntl
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

from argparse import ArgumentParser, ArgumentError
from pathlib import Path
from typing import Any, Dict, List, NoReturn, Optional, Set, TextIO, Tuple, Union, cast

# generic JSON type that avoids getter issues; proper use is being enforced by
# return types of methods and typing vars holding return values of json getters
Json = Dict[str, Any]

###
# Constants
##

MARKERS: List[str] = [".git", "ROOT", "WORKSPACE"]
SYSTEM_ROOT: str = os.path.abspath(os.sep)
ALT_DIRS: List[str] = ["target_root", "rule_root", "expression_root"]
REPO_ROOTS: List[str] = ["repository"] + ALT_DIRS
DEFAULT_BUILD_ROOT: str = os.path.join(Path.home(), ".cache/just")
DEFAULT_GIT_BIN: str = "git"  # to be taken from PATH
DEFAULT_LAUNCHER: List[str] = ["env", "--"]

DEFAULT_REPO: Json = {"": {"repository": {"type": "file", "path": "."}}}

DEFAULT_INPUT_CONFIG_NAME: str = "repos.in.json"
DEFAULT_JUSTMR_CONFIG_NAME: str = "repos.json"
DEFAULT_CONFIG_DIRS: List[str] = [".", "./etc"]
"""Directories where to look for configuration file inside a root"""

GIT_NOBODY_ENV: Dict[str, str] = {
    "GIT_AUTHOR_DATE": "1970-01-01T00:00Z",
    "GIT_AUTHOR_NAME": "Nobody",
    "GIT_AUTHOR_EMAIL": "nobody@example.org",
    "GIT_COMMITTER_DATE": "1970-01-01T00:00Z",
    "GIT_COMMITTER_NAME": "Nobody",
    "GIT_COMMITTER_EMAIL": "nobody@example.org",
    "GIT_CONFIG_GLOBAL": "/dev/null",
    "GIT_CONFIG_SYSTEM": "/dev/null",
}

###
# Global vars
##

g_ROOT: str = DEFAULT_BUILD_ROOT
"""The configured local build root"""
g_GIT: str = DEFAULT_GIT_BIN
"""Git binary to use"""
g_LAUNCHER: List[str] = DEFAULT_LAUNCHER
"""Local launcher to use for commands provided in imports"""

###
# System utils
##


def log(*args: str, **kwargs: Any) -> None:
    print(*args, file=sys.stderr, **kwargs)


def fail(s: str, exit_code: int = 1) -> NoReturn:
    """Log as error and exit. Matches the color scheme of 'just-mr'."""
    # Color is fmt::color::red
    log(f"\033[38;2;255;0;0mERROR:\033[0m {s}")
    sys.exit(exit_code)


def warn(s: str) -> None:
    """Log as warning. Matches the color scheme of 'just-mr'."""
    # Color is fmt::color::orange
    log(f"\033[38;2;255;0;0mWARN:\033[0m {s}")


def report(s: Optional[str]) -> None:
    """Log as information message. Matches the color scheme of 'just-mr'."""
    # Color is fmt::color::lime_green
    log("" if s is None else f"\033[38;2;50;205;50mINFO:\033[0m {s}")


def run_cmd(
    cmd: List[str],
    *,
    env: Optional[Any] = None,
    stdout: Optional[Any] = subprocess.DEVNULL,
    stdin: Optional[Any] = None,
    cwd: str,
    attempts: int = 1,
    fail_context: Optional[str] = None,
) -> Tuple[bytes, int]:
    """Run a specific command. If fail_context string given, exit on failure.
    Expects fail_context to end in a newline."""
    attempts = max(attempts, 1)  # at least one attempt
    result: Any = None
    for _ in range(attempts):
        result = subprocess.run(cmd,
                                cwd=cwd,
                                env=env,
                                stdout=stdout,
                                stdin=stdin)
        if result.returncode == 0:
            return result.stdout, result.returncode  # return successful result
    if fail_context is not None:
        fail("%sCommand %s in %s failed after %d attempt%s" %
             (fail_context, cmd, cwd, attempts, "" if attempts == 1 else "s"))
    return result.stdout, result.returncode  # return result of last failure


def try_rmtree(tree: str) -> None:
    """Safely remove a directory tree."""
    for _ in range(10):
        try:
            shutil.rmtree(tree)
            return
        except:
            time.sleep(1.0)
    fail("Failed to remove %s" % (tree, ))


def lock_acquire(fpath: str, is_shared: bool = False) -> TextIO:
    """Acquire a lock on a file descriptor. It opens a stream with shared access
    for given file path and returns it to keep it alive. The lock can only be
    released by calling lock_release() on this stream."""
    if os.path.exists(fpath):
        if os.path.isdir(fpath):
            fail("Lock path %s is a directory!" % (fpath, ))
    else:
        os.makedirs(Path(fpath).parent, exist_ok=True)
    lockfile = open(fpath, "a+")  # allow shared read and create if on first try
    fcntl.flock(lockfile.fileno(),
                fcntl.LOCK_SH if is_shared else fcntl.LOCK_EX)
    return lockfile


def lock_release(lockfile: TextIO) -> None:
    """Release lock on the file descriptor of the given open stream, then close
    the stream. Expects the argument to be the output of a previous
    lock_acquire() call."""
    fcntl.flock(lockfile.fileno(), fcntl.LOCK_UN)
    lockfile.close()


###
# Config utils
##


def find_workspace_root() -> Optional[str]:
    """Find the workspace root of the current working directory."""
    def is_workspace_root(path: str) -> bool:
        for m in MARKERS:
            if os.path.exists(os.path.join(path, m)):
                return True
        return False

    path: str = os.getcwd()
    while True:
        if is_workspace_root(path):
            return path
        if path == SYSTEM_ROOT:
            return None
        path = os.path.dirname(path)


def get_repository_config_file(filename: str,
                               root: Optional[str] = None) -> Optional[str]:
    """Get a named configuration file relative to a root."""
    if not root:
        root = find_workspace_root()
        if not root:
            return None
    for dir in DEFAULT_CONFIG_DIRS:
        path: str = os.path.realpath(os.path.join(root, dir, filename))
        if os.path.isfile(path):
            return path


###
# Imports utils
##


def get_repo_to_import(config: Json) -> str:
    """From a given repository config, take the main repository."""
    main = config.get("main")
    if main is not None:
        if not isinstance(main, str):
            fail("Foreign config contains malformed \"main\" field:\n%r" %
                 (json.dumps(main, indent=2), ))
        return main
    repos = config.get("repositories", {})
    if repos and isinstance(repos, dict):
        # take main repo as first named lexicographically
        return sorted(repos.keys())[0]
    fail("Config does not contain any repositories; unsure what to import")


def get_base_repo_if_computed(repo: Json) -> Optional[str]:
    """If repository is computed, return the base repository name."""
    if repo.get("type") == "computed":
        return cast(str, repo.get("repo"))
    return None


def repos_to_import(repos_config: Json, entry: str,
                    known: Set[str]) -> Tuple[List[str], List[str]]:
    """Compute the set of transitively reachable repositories and the collection
    of repositories additionally needed as they serve as layers for the
    repositories to import."""
    to_import: Set[str] = set()
    extra_imports: Set[str] = set()

    def visit(name: str) -> None:
        # skip any existing or already visited repositories
        if name in known or name in to_import:
            return
        repo_desc: Json = repos_config.get(name, {})

        # if proper import, visit bindings, which are fully imported
        if name not in extra_imports:
            to_import.add(name)

            vals = cast(Dict[str, str], repo_desc.get("bindings", {})).values()
            for n in vals:
                extra_imports.discard(n)
                visit(n)

        repo = repo_desc.get("repository")
        if isinstance(repo, str):
            # visit referred repository, but skip bindings
            if repo not in known and repo not in to_import:
                extra_imports.add(repo)
                visit(repo)
        else:
            # if computed, visit the referred repository
            repo_base = get_base_repo_if_computed(cast(Json, repo))
            if repo_base is not None:
                extra_imports.discard(repo_base)
                visit(repo_base)

        # add layers as extra imports, but referred repositories of computed
        # layers need to be fully imported
        for layer in ALT_DIRS:
            if layer in repo_desc:
                extra: str = repo_desc[layer]
                if extra not in known and extra not in to_import:
                    extra_imports.add(extra)
                extra_repo_base = get_base_repo_if_computed(
                    cast(Json,
                         repos_config.get(extra, {}).get("repository", {})))
                if extra_repo_base is not None:
                    extra_imports.discard(extra_repo_base)
                    visit(extra_repo_base)

    visit(entry)
    return list(to_import), list(extra_imports)


def name_imports(to_import: List[str],
                 extra_imports: List[str],
                 existing: Set[str],
                 base_name: str,
                 main: Optional[str] = None) -> Dict[str, str]:
    """Assign names to the repositories to import in such a way that
    no conflicts arise."""
    assign: Dict[str, str] = {}

    def find_name(name: str) -> str:
        base: str = "%s/%s" % (base_name, name)
        if (base not in existing) and (base not in assign):
            return base
        count: int = 0
        while True:
            count += 1
            candidate: str = base + " (%d)" % count
            if (candidate not in existing) and (candidate not in assign):
                return candidate

    if main is not None and (base_name not in existing):
        assign[main] = base_name
        to_import = [x for x in to_import if x != main]
        extra_imports = [x for x in extra_imports if x != main]
    for repo in to_import + extra_imports:
        assign[repo] = find_name(repo)
    return assign


def rewrite_file_repo(repo: Json, remote_type: str,
                      remote_stub: Dict[str, Any]) -> Json:
    """Rewrite \"file\"-type descriptions based on remote type."""
    if remote_type == "git":
        # for imports from Git, file repos become type 'git' with subdir
        changes = {}
        subdir = repo.get("path", ".")
        if subdir not in ["", "."]:
            changes["subdir"] = subdir
        return dict(remote_stub, **changes)
    elif remote_type == "file":
        # for imports from local checkouts, file repos remain type 'file'
        changes = {}
        root: str = remote_stub["path"]
        path = repo.get("path", ".")
        if not Path(path).is_absolute():
            changes["path"] = str(Path(root) / path)
        return dict(repo, **changes)
    elif remote_type in ["archive", "zip"]:
        # for imports from archives, file repos become archive type with subdir;
        # any path is prepended by the subdir provided in the input file, if any
        changes = {}
        subdir = repo.get("path", ".")
        if subdir not in ["", "."]:
            existing = remote_stub.get("subdir", ".")
            if existing not in ["", "."]:
                subdir = os.path.join(existing, subdir)
            changes["subdir"] = subdir
        return dict(remote_stub, **changes)
    fail("Unsupported remote type!")


def rewrite_repo(repo_spec: Json,
                 *,
                 remote_type: str,
                 remote_stub: Dict[str, Any],
                 assign: Json,
                 absent: bool,
                 as_layer: bool = False) -> Json:
    """Rewrite description of imported repositories."""
    new_spec: Json = {}
    repo = repo_spec.get("repository", {})
    if isinstance(repo, str):
        repo = assign[repo]
    elif repo.get("type") == "file":
        # "file"-type repositories need to be rewritten based on remote type
        repo = rewrite_file_repo(repo, remote_type, remote_stub)
    elif repo.get("type") == "distdir":
        existing_repos: List[str] = repo.get("repositories", [])
        new_repos = [assign[k] for k in existing_repos]
        repo = dict(repo, **{"repositories": new_repos})
    elif repo.get("type") == "computed":
        target: str = repo.get("repo", None)
        repo = dict(repo, **{"repo": assign[target]})
    if absent and isinstance(repo, dict):
        repo["pragma"] = dict(repo.get("pragma", {}), **{"absent": True})
    new_spec["repository"] = repo
    # rewrite other roots and bindings, if actually needed to be imported
    if not as_layer:
        for key in ["target_root", "rule_root", "expression_root"]:
            if key in repo_spec:
                new_spec[key] = assign[repo_spec[key]]
        for key in [
                "target_file_name", "rule_file_name", "expression_file_name"
        ]:
            if key in repo_spec:
                new_spec[key] = repo_spec[key]
        bindings = repo_spec.get("bindings", {})
        new_bindings = {}
        for k, v in bindings.items():
            new_bindings[k] = assign[v]
        if new_bindings:
            new_spec["bindings"] = new_bindings
    return new_spec


def handle_import(remote_type: str, remote_stub: Dict[str, Any],
                  repo_desc: Json, core_repos: Json, foreign_config: Json, *,
                  fail_context: str) -> Json:
    """General handling of repository import from a foreign config."""

    # parse input description
    import_as: Optional[str] = repo_desc.get("alias", None)
    if import_as is not None and not isinstance(import_as, str):
        fail(
            fail_context +
            "Expected \"repos\" entry subfield \"import_as\" to be a string, but found:\n%r"
            % (json.dumps(import_as, indent=2), ))

    foreign_name: Optional[str] = repo_desc.get("repo", None)
    if foreign_name is not None and not isinstance(foreign_name, str):
        fail(
            fail_context +
            "Expected \"repos\" entry subfield \"repo\" to be a string, but found:\n%r"
            % (json.dumps(foreign_name, indent=2), ))
    if foreign_name is None:
        # if not provided, get main repository from source config
        foreign_name = get_repo_to_import(foreign_config)

    import_map: Json = repo_desc.get("map", None)
    if import_map is None:
        import_map = {}
    elif not isinstance(import_map, dict):
        fail(
            fail_context +
            "Expected \"repos\" entry subfield \"map\" to be a map, but found:\n%r"
            % (json.dumps(import_map, indent=2), ))

    pragma: Json = repo_desc.get("pragma", None)
    if pragma is not None and not isinstance(pragma, dict):
        fail(
            fail_context +
            "Expected \"repos\" entry subfield \"pragma\" to be a map, but found:\n%r"
            % (json.dumps(pragma, indent=2), ))
    absent: bool = False if pragma is None else pragma.get("absent", False)
    if absent is None:
        absent = False
    elif not isinstance(absent, bool):
        fail(
            fail_context +
            "Expected \"repos\" entry pragma \"absent\" to be a bool, but found:\n%r"
            % (json.dumps(absent, indent=2), ))

    # Handle import with renaming
    foreign_repos: Json = foreign_config.get("repositories", {})
    if foreign_repos is None or not isinstance(foreign_repos, dict):
        fail(
            fail_context +
            "Found empty or malformed \"repositories\" field in source configuration file"
        )
    main_repos, extra_imports = repos_to_import(foreign_repos, foreign_name,
                                                set(import_map.keys()))
    extra_repos = sorted([x for x in main_repos if x != foreign_name])
    ordered_imports: List[str] = [foreign_name] + extra_repos
    extra_imports = sorted(extra_imports)

    import_name = import_as if import_as is not None else foreign_name
    assign: Dict[str, str] = name_imports(ordered_imports,
                                          extra_imports,
                                          set(core_repos.keys()),
                                          import_name,
                                          main=foreign_name)

    # Report progress
    report("Importing %r as %r" % (foreign_name, import_name))
    report("\tTransitive dependencies to import: %r" % (extra_repos, ))
    report("\tRepositories imported as layers: %r" % (extra_imports, ))
    report(None)  # adds newline

    total_assign = dict(assign, **import_map)
    for repo in ordered_imports:
        core_repos[assign[repo]] = rewrite_repo(foreign_repos[repo],
                                                remote_type=remote_type,
                                                remote_stub=remote_stub,
                                                assign=total_assign,
                                                absent=absent)
    for repo in extra_imports:
        core_repos[assign[repo]] = rewrite_repo(foreign_repos[repo],
                                                remote_type=remote_type,
                                                remote_stub=remote_stub,
                                                assign=total_assign,
                                                absent=absent,
                                                as_layer=True)

    return core_repos


###
# Git utils
##


def gc_repo_lock_acquire(is_shared: bool = False) -> TextIO:
    """Acquire garbage collector file lock for the Git cache."""
    # use same naming scheme as in Just
    return lock_acquire(os.path.join(g_ROOT, "repositories/gc.lock"), is_shared)


def git_root(*, upstream: Optional[str]) -> str:
    """Get the root of specified upstream repository. Passing None always
    returns the root of the Git cache repository. No checks are made on the
    returned path."""
    return (os.path.join(g_ROOT, "repositories/generation-0/git")
            if upstream is None else upstream)


def git_keep(commit: str, *, upstream: Optional[str],
             fail_context: str) -> None:
    """Keep commit by tagging it."""
    git_env = {**os.environ, **GIT_NOBODY_ENV}
    run_cmd(g_LAUNCHER + [
        g_GIT, "tag", "-f", "-m", "Keep referenced tree alive",
        "keep-%s" % (commit, ), commit
    ],
            cwd=git_root(upstream=upstream),
            env=git_env,
            attempts=3,
            fail_context=fail_context)


def ensure_git_init(*,
                    upstream: Optional[str],
                    init_bare: bool = True,
                    fail_context: str) -> None:
    """Ensure Git repository given by upstream is initialized. Use an exclusive
    lock to ensure the initialization happens only once."""
    root: str = git_root(upstream=upstream)
    # acquire exclusive lock; use same naming scheme as in Just
    lockfile = lock_acquire(os.path.join(Path(root).parent, "init_open.lock"))
    # do the critical work
    if os.path.exists(root):
        return
    os.makedirs(root)
    git_init_cmd: List[str] = [g_GIT, "init"]
    if init_bare:
        git_init_cmd += ["--bare"]
    run_cmd(g_LAUNCHER + git_init_cmd, cwd=root, fail_context=fail_context)
    # release the exclusive lock
    lock_release(lockfile)


def git_commit_present(commit: str, *, upstream: Optional[str]) -> bool:
    """Check if commit is present in specified Git repository."""
    result = subprocess.run(g_LAUNCHER + [g_GIT, "show", "--oneline", commit],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL,
                            cwd=git_root(upstream=upstream))
    return result.returncode == 0


def git_url_is_path(url: str) -> Optional[str]:
    """Get the path a URL refers to if it is in a supported path format,
    and None otherwise."""
    if url.startswith('/'):
        return url
    if url.startswith('./'):
        return url[len('./'):]
    if url.startswith('file://'):
        return url[len('file://'):]
    return None


def git_fetch(*, from_repo: Optional[str], to_repo: Optional[str],
              fetchable: str, fail_context: Optional[str]) -> bool:
    """Fetch from a given repository a fetchable object (branch or commit) into
    another repository. A None value for a repository means the Git cache
    repository is used. Returns success flag of fetch command."""
    if from_repo is None:
        from_repo = git_root(upstream=None)
    else:
        path_url = git_url_is_path(from_repo)
        if path_url is not None:
            from_repo = os.path.abspath(path_url)
    return run_cmd(g_LAUNCHER + [g_GIT, "fetch", from_repo, fetchable],
                   cwd=git_root(upstream=to_repo),
                   fail_context=fail_context)[1] == 0


###
# CAS utils
##


def gc_storage_lock_acquire(is_shared: bool = False) -> TextIO:
    """Acquire garbage collector file lock for the local storage."""
    # use same naming scheme as in Just
    return lock_acquire(os.path.join(g_ROOT, "protocol-dependent", "gc.lock"),
                        is_shared)


def git_hash(content: bytes, type: str = "blob") -> Tuple[str, bytes]:
    """Hash content as a Git object. Returns the hash, as well as the header to
    be stored."""
    header = "{} {}\0".format(type, len(content)).encode('utf-8')
    h = hashlib.sha1()
    h.update(header)
    h.update(content)
    return h.hexdigest(), header


def add_to_cas(data: Union[str, bytes]) -> Tuple[str, str]:
    """Add content to local file CAS and return its CAS location and hash."""
    try:
        if isinstance(data, str):
            data = data.encode('utf-8')
        h, _ = git_hash(data)
        cas_root = os.path.join(
            g_ROOT, f"protocol-dependent/generation-0/git-sha1/casf/{h[0:2]}")
        basename = h[2:]
        target = os.path.join(cas_root, basename)
        tempname = os.path.join(cas_root, "%s.%d" % (basename, os.getpid()))

        if os.path.exists(target):
            return target, h

        os.makedirs(cas_root, exist_ok=True)
        with open(tempname, "wb") as f:
            f.write(data)
            f.flush()
            os.chmod(f.fileno(), 0o444)
            os.fsync(f.fileno())
        os.utime(tempname, (0, 0))
        os.rename(tempname, target)
        return target, h
    except Exception as ex:
        fail("Adding content to CAS failed with:\n%r" % (ex, ))


def cas_path(h: str) -> str:
    """Get path to local file CAS."""
    return os.path.join(
        g_ROOT, f"protocol-dependent/generation-0/git-sha1/casf/{h[0:2]}",
        h[2:])


def is_in_cas(h: str) -> bool:
    """Check if content is in local file CAS."""
    return os.path.exists(cas_path(h))


###
# Import from Git
##


def git_checkout(url: str, branch: str, *, commit: Optional[str],
                 mirrors: List[str], inherit_env: List[str],
                 fail_context: str) -> Tuple[str, Dict[str, Any], str]:
    """Fetch a given remote Git repository and checkout a specified branch.
    Return the checkout location, the repository description stub to use for
    rewriting 'file'-type dependencies, and the temp dir to later clean up."""
    workdir: str = tempfile.mkdtemp()
    srcdir: str = os.path.join(workdir, "src")

    fail_context += "While checking out branch %r of %r:\n" % (branch, url)
    if commit is None:
        # If no commit given, do shallow clone and get HEAD commit from
        # definitive source location
        fetch_url = git_url_is_path(url)
        if fetch_url is None:
            fetch_url = url
        else:
            fetch_url = os.path.abspath(fetch_url)
        run_cmd(
            g_LAUNCHER +
            [g_GIT, "clone", "-b", branch, "--depth", "1", fetch_url, "src"],
            cwd=workdir,
            fail_context=fail_context)
        commit = run_cmd(g_LAUNCHER + [g_GIT, "log", "-n", "1", "--pretty=%H"],
                         cwd=srcdir,
                         stdout=subprocess.PIPE,
                         fail_context=fail_context)[0].decode('utf-8').strip()
        report("Importing remote Git commit %s" % (commit, ))
        # Cache this commit by fetching it to Git cache and tagging it
        ensure_git_init(upstream=None, fail_context=fail_context)
        git_fetch(from_repo=srcdir,
                  to_repo=None,
                  fetchable=commit,
                  fail_context=fail_context)
        git_keep(commit, upstream=None, fail_context=fail_context)
    else:
        if not git_commit_present(commit, upstream=None):
            # If commit not in Git cache repository, fetch witnessing branch
            # from remote into the Git cache repository. Try mirrors first, as
            # they are closer
            ensure_git_init(upstream=None, fail_context=fail_context)
            fetched: bool = False
            for source in mirrors + [url]:
                if git_fetch(from_repo=source,
                             to_repo=None,
                             fetchable=branch,
                             fail_context=None) and git_commit_present(
                                 commit, upstream=None):
                    fetched = True
                    break
            if not fetched:
                fail(fail_context +
                     "Failed to fetch commit %s.\nTried locations:\n%s" % (
                         commit,
                         "\n".join(["\t%s" % (x, ) for x in mirrors + [url]]),
                     ))
            git_keep(commit, upstream=None, fail_context=fail_context)
        # Create checkout from commit in Git cache repository
        ensure_git_init(upstream=srcdir,
                        init_bare=False,
                        fail_context=fail_context)
        git_fetch(from_repo=None,
                  to_repo=srcdir,
                  fetchable=commit,
                  fail_context=fail_context)
        run_cmd(g_LAUNCHER + [g_GIT, "checkout", commit],
                cwd=srcdir,
                fail_context=fail_context)

    # Prepare the description stub used to rewrite "file"-type dependencies
    repo_stub: Dict[str, Any] = {
        "type": "git",
        "repository": url,
        "branch": branch,
        "commit": commit,
    }
    if mirrors:
        repo_stub = dict(repo_stub, **{"mirrors": mirrors})
    if inherit_env:
        repo_stub = dict(repo_stub, **{"inherit env": inherit_env})
    return srcdir, repo_stub, workdir


def import_from_git(core_repos: Json, imports_entry: Json) -> Json:
    """Handles imports from Git repositories."""
    # Set granular logging message
    fail_context: str = "While importing from source \"git\":\n"

    # Get the repositories list
    repos: List[Any] = imports_entry.get("repos", [])
    if not isinstance(repos, list):
        fail(fail_context +
             "Expected field \"repos\" to be a list, but found:\n%r" %
             (json.dumps(repos, indent=2), ))

    # Check if anything is to be done
    if not repos:  # empty
        return core_repos

    # Parse source config fields
    url: str = imports_entry.get("url", None)
    if not isinstance(url, str):
        fail(fail_context +
             "Expected field \"url\" to be a string, but found:\n%r" %
             (json.dumps(url, indent=2), ))

    branch: str = imports_entry.get("branch", None)
    if not isinstance(branch, str):
        fail(fail_context +
             "Expected field \"branch\" to be a string, but found:\n%r" %
             (json.dumps(branch, indent=2), ))

    commit: Optional[str] = imports_entry.get("commit", None)
    if commit is not None and not isinstance(commit, str):
        fail(fail_context +
             "Expected field \"commit\" to be a string, but found:\n%r" %
             (json.dumps(commit, indent=2), ))

    mirrors: Optional[List[str]] = imports_entry.get("mirrors", [])
    if mirrors is not None and not isinstance(mirrors, list):
        fail(fail_context +
             "Expected field \"mirrors\" to be a list, but found:\n%r" %
             (json.dumps(mirrors, indent=2), ))

    inherit_env: Optional[List[str]] = imports_entry.get("inherit_env", [])
    if inherit_env is not None and not isinstance(inherit_env, list):
        fail(fail_context +
             "Expected field \"inherit_env\" to be a list, but found:\n%r" %
             (json.dumps(inherit_env, indent=2), ))

    as_plain: Optional[bool] = imports_entry.get("as_plain", False)
    if as_plain is not None and not isinstance(as_plain, bool):
        fail(fail_context +
             "Expected field \"as_plain\" to be a bool, but found:\n%r" %
             (json.dumps(as_plain, indent=2), ))

    foreign_config_file: Optional[str] = imports_entry.get("config", None)
    if foreign_config_file is not None and not isinstance(
            foreign_config_file, str):
        fail(fail_context +
             "Expected field \"config\" to be a string, but found:\n%r" %
             (json.dumps(foreign_config_file, indent=2), ))

    # Fetch the source Git repository
    srcdir, remote_stub, to_clean_up = git_checkout(url,
                                                    branch,
                                                    commit=commit,
                                                    mirrors=mirrors,
                                                    inherit_env=inherit_env,
                                                    fail_context=fail_context)

    # Read in the foreign config file
    if foreign_config_file:
        foreign_config_file = os.path.join(srcdir, foreign_config_file)
    else:
        foreign_config_file = get_repository_config_file(
            DEFAULT_JUSTMR_CONFIG_NAME, srcdir)
    foreign_config: Json = {}
    if as_plain:
        foreign_config = {"main": "", "repositories": DEFAULT_REPO}
    else:
        if (foreign_config_file):
            try:
                with open(foreign_config_file) as f:
                    foreign_config = json.load(f)
            except OSError:
                fail(fail_context + "Failed to open foreign config file %s" %
                     (foreign_config_file, ))
            except Exception as ex:
                fail(fail_context +
                     "Reading foreign config file failed with:\n%r" % (ex, ))
        else:
            fail(fail_context +
                 "Failed to find the repository configuration file!")

    # Process the imported repositories, in order
    for repo_entry in repos:
        if not isinstance(repo_entry, dict):
            fail(fail_context +
                 "Expected \"repos\" entries to be objects, but found:\n%r" %
                 (json.dumps(repo_entry, indent=2), ))
        repo_entry = cast(Json, repo_entry)

        core_repos = handle_import("git",
                                   remote_stub,
                                   repo_entry,
                                   core_repos,
                                   foreign_config,
                                   fail_context=fail_context)

    # Clean up local fetch
    try_rmtree(to_clean_up)
    return core_repos


###
# Import from file
##


def import_from_file(core_repos: Json, imports_entry: Json) -> Json:
    """Handles imports from a local checkout."""
    # Set granular logging message
    fail_context: str = "While importing from source \"file\":\n"

    # Get the repositories list
    repos: List[Any] = imports_entry.get("repos", [])
    if not isinstance(repos, list):
        fail(fail_context +
             "Expected field \"repos\" to be a list, but found:\n%r" %
             (json.dumps(repos, indent=2), ))

    # Check if anything is to be done
    if not repos:  # empty
        return core_repos

    # Parse source config fields
    path: str = imports_entry.get("path", None)
    if not isinstance(path, str):
        fail(fail_context +
             "Expected field \"path\" to be a string, but found:\n%r" %
             (json.dumps(path, indent=2), ))

    as_plain: Optional[bool] = imports_entry.get("as_plain", False)
    if as_plain is not None and not isinstance(as_plain, bool):
        fail(fail_context +
             "Expected field \"as_plain\" to be a bool, but found:\n%r" %
             (json.dumps(as_plain, indent=2), ))

    foreign_config_file: Optional[str] = imports_entry.get("config", None)
    if foreign_config_file is not None and not isinstance(
            foreign_config_file, str):
        fail(fail_context +
             "Expected field \"config\" to be a string, but found:\n%r" %
             (json.dumps(foreign_config_file, indent=2), ))

    # Read in the foreign config file
    if foreign_config_file:
        foreign_config_file = os.path.join(path, foreign_config_file)
    else:
        foreign_config_file = get_repository_config_file(
            DEFAULT_JUSTMR_CONFIG_NAME, path)
    foreign_config: Json = {}
    if as_plain:
        foreign_config = {"main": "", "repositories": DEFAULT_REPO}
    else:
        if (foreign_config_file):
            try:
                with open(foreign_config_file) as f:
                    foreign_config = json.load(f)
            except OSError:
                fail(fail_context + "Failed to open foreign config file %s" %
                     (foreign_config_file, ))
            except Exception as ex:
                fail(fail_context +
                     "Reading foreign config file failed with:\n%r" % (ex, ))
        else:
            fail(fail_context +
                 "Failed to find the repository configuration file!")

    # Prepare the description stub used to rewrite "file"-type dependencies
    remote_stub: Dict[str, Any] = {
        "type": "file",
        "path": path,
    }

    # Process the imported repositories, in order
    for repo_entry in repos:
        if not isinstance(repo_entry, dict):
            fail(fail_context +
                 "Expected \"repos\" entries to be objects, but found:\n%r" %
                 (json.dumps(repo_entry, indent=2), ))
        repo_entry = cast(Json, repo_entry)

        core_repos = handle_import("file",
                                   remote_stub,
                                   repo_entry,
                                   core_repos,
                                   foreign_config,
                                   fail_context=fail_context)

    return core_repos


###
# Import from archive
##


def archive_fetch(locations: List[str],
                  *,
                  content: Optional[str],
                  sha256: Optional[str] = None,
                  sha512: Optional[str] = None,
                  fail_context: str) -> str:
    """Make sure an archive is available in local CAS. Try all the remote
    locations given. Return the content hash on success."""
    if content is None or not is_in_cas(content):
        fetched: bool = False
        for source in locations:
            data, err_code = run_cmd(g_LAUNCHER + ["wget", "-O", "-", source],
                                     stdout=subprocess.PIPE,
                                     cwd=os.getcwd())
            if err_code == 0:
                # Compare with checksums, if given
                if sha256 is not None:
                    actual_hash = hashlib.sha256(data).hexdigest()
                    if sha256 != actual_hash:
                        continue
                if sha512 is not None:
                    actual_hash = hashlib.sha512(data).hexdigest()
                    if sha512 != actual_hash:
                        continue
                # Add to CAS and compare with expected content, if given
                _, computed_hash = add_to_cas(data)
                if content is not None:
                    if content != computed_hash:
                        continue
                    fetched = True
                    break
                else:
                    content = computed_hash
                    fetched = True
                    break

        if not fetched:
            fail(fail_context +
                 "Failed to fetch archive.\nTried locations:\n%s" %
                 ("\n".join(["\t%s" % (x, ) for x in locations]), ))

    return cast(str, content)


def unpack_archive(content_id: str, *, archive_type: str, unpack_to: str,
                   fail_context: str) -> None:
    """Unpack archive stored as a local CAS blob into a given directory."""
    fail_context += "While unpacking archive %s:\n" % (cas_path(content_id), )
    # ensure destination path is valid
    if os.path.exists(unpack_to):
        if not os.path.isdir(unpack_to):
            fail(fail_context +
                 "Unpack location %s exists and is not a directory!" %
                 (unpack_to, ))
        if os.listdir(unpack_to):
            fail(fail_context + "Cannot unpack to nonempty directory %s" %
                 (unpack_to, ))
    else:
        os.makedirs(unpack_to, exist_ok=True)
    # unpack based on archive type
    if archive_type == "zip":
        # try as zip and 7z archives
        if run_cmd(g_LAUNCHER + ["unzip", "-d", ".",
                                 cas_path(content_id)],
                   cwd=unpack_to,
                   fail_context=None)[1] != 0 and run_cmd(
                       g_LAUNCHER + ["7z", "x", cas_path(content_id)],
                       cwd=unpack_to,
                       fail_context=None)[1] != 0:
            fail(fail_context + "Failed to extract zip-like archive %s" %
                 (cas_path(content_id), ))
    else:
        # try as tarball
        if run_cmd(g_LAUNCHER + ["tar", "xf", cas_path(content_id)],
                   cwd=unpack_to,
                   fail_context=None)[1] != 0:
            fail(fail_context + "Failed to extract tarball %s" %
                 (cas_path(content_id), ))
    return


def archive_checkout(fetch: str, *, archive_type: str, content: Optional[str],
                     mirrors: List[str], sha256: Optional[str],
                     sha512: Optional[str], subdir: Optional[str],
                     fail_context: str) -> Tuple[str, Dict[str, Any], str]:
    """Fetch a given remote archive to local CAS, unpack it, check content,
    and return the checkout location."""
    fail_context += "While checking out archive %r:\n" % (fetch)
    if content is None:
        # If content is not known, get it from the definitive source location
        content = archive_fetch([fetch],
                                content=None,
                                sha256=sha256,
                                sha512=sha512,
                                fail_context=fail_context)
    else:
        # If content known, try the mirrors first, as they are closer
        archive_fetch(mirrors + [fetch],
                      content=content,
                      sha256=sha256,
                      sha512=sha512,
                      fail_context=fail_context)

    workdir: str = tempfile.mkdtemp()
    unpack_archive(content,
                   archive_type=archive_type,
                   unpack_to=workdir,
                   fail_context=fail_context)
    srcdir = (workdir if subdir is None else os.path.join(workdir, subdir))

    # Prepare the description stub used to rewrite "file"-type dependencies
    repo_stub: Dict[str, Any] = {
        "type": "zip" if archive_type == "zip" else "archive",
        "fetch": fetch,
        "content": content,
    }
    if mirrors:
        repo_stub = dict(repo_stub, **{"mirrors": mirrors})
    if sha256 is not None:
        repo_stub = dict(repo_stub, **{"sha256": sha256})
    if sha512 is not None:
        repo_stub = dict(repo_stub, **{"sha512": sha512})
    if subdir is not None:
        repo_stub = dict(repo_stub, **{"subdir": subdir})
    return srcdir, repo_stub, workdir


def import_from_archive(core_repos: Json, imports_entry: Json) -> Json:
    """Handles imports from archive-type repositories."""
    # Set granular logging message
    fail_context: str = "While importing from source \"archive\":\n"

    # Get the repositories list
    repos: List[Any] = imports_entry.get("repos", [])
    if not isinstance(repos, list):
        fail(fail_context +
             "Expected field \"repos\" to be a list, but found:\n%r" %
             (json.dumps(repos, indent=2), ))

    # Check if anything is to be done
    if not repos:  # empty
        return core_repos

    # Parse source config fields
    fetch: str = imports_entry.get("fetch", None)
    if not isinstance(fetch, str):
        fail(fail_context +
             "Expected field \"fetch\" to be a string, but found:\n%r" %
             (json.dumps(fetch, indent=2), ))

    archive_type: str = "archive"  # type according to 'just-mr'
    tmp_type: Optional[str] = imports_entry.get("type", None)
    if tmp_type is not None:
        if not isinstance(tmp_type, str):
            fail(fail_context +
                 "Expected field \"type\" to be a string, but found:\n%r" %
                 (json.dumps(tmp_type, indent=2), ))
        if tmp_type not in ["tar", "zip"]:  # values expected in input file
            warn(
                fail_context +
                "Field \"type\" has unsupported value %r\nTrying with default value 'tar'"
                % (json.dumps(tmp_type), ))
        else:
            archive_type = "zip" if tmp_type == "zip" else "archive"

    content: Optional[str] = imports_entry.get("content", None)
    if content is not None and not isinstance(content, str):
        fail(fail_context +
             "Expected field \"content\" to be a string, but found:\n%r" %
             (json.dumps(content, indent=2), ))

    mirrors: Optional[List[str]] = imports_entry.get("mirrors", [])
    if mirrors is not None and not isinstance(mirrors, list):
        fail(fail_context +
             "Expected field \"mirrors\" to be a list, but found:\n%r" %
             (json.dumps(mirrors, indent=2), ))

    sha256: Optional[str] = imports_entry.get("sha256", None)
    if sha256 is not None and not isinstance(sha256, str):
        fail(fail_context +
             "Expected field \"sha256\" to be a string, but found:\n%r" %
             (json.dumps(sha256, indent=2), ))

    sha512: Optional[str] = imports_entry.get("sha512", None)
    if sha512 is not None and not isinstance(sha512, str):
        fail(fail_context +
             "Expected field \"sha512\" to be a string, but found:\n%r" %
             (json.dumps(sha512, indent=2), ))

    subdir: Optional[str] = imports_entry.get("subdir", None)
    if subdir is not None:
        if not isinstance(subdir, str):
            fail(fail_context +
                 "Expected field \"subdir\" to be a string, but found:\n%r" %
                 (json.dumps(subdir, indent=2), ))
        if subdir in ["", "."]:
            subdir = None  # treat as if missing
        elif os.path.isabs(subdir):
            fail(
                fail_context +
                "Expected field \"subdir\" to be a relative path, but found:\n%r"
                % (json.dumps(subdir, indent=2), ))

    as_plain: Optional[bool] = imports_entry.get("as_plain", False)
    if as_plain is not None and not isinstance(as_plain, bool):
        fail(fail_context +
             "Expected field \"as_plain\" to be a bool, but found:\n%r" %
             (json.dumps(as_plain, indent=2), ))

    foreign_config_file: Optional[str] = imports_entry.get("config", None)
    if foreign_config_file is not None and not isinstance(
            foreign_config_file, str):
        fail(fail_context +
             "Expected field \"config\" to be a string, but found:\n%r" %
             (json.dumps(foreign_config_file, indent=2), ))

    # Fetch archive to local CAS and unpack
    srcdir, remote_stub, to_clean_up = archive_checkout(
        fetch,
        archive_type=archive_type,
        content=content,
        mirrors=mirrors,
        sha256=sha256,
        sha512=sha512,
        subdir=subdir,
        fail_context=fail_context)

    # Read in the foreign config file
    if foreign_config_file:
        foreign_config_file = os.path.join(srcdir, foreign_config_file)
    else:
        foreign_config_file = get_repository_config_file(
            DEFAULT_JUSTMR_CONFIG_NAME, srcdir)
    foreign_config: Json = {}
    if as_plain:
        foreign_config = {"main": "", "repositories": DEFAULT_REPO}
    else:
        if (foreign_config_file):
            try:
                with open(foreign_config_file) as f:
                    foreign_config = json.load(f)
            except OSError:
                fail(fail_context + "Failed to open foreign config file %s" %
                     (foreign_config_file, ))
            except Exception as ex:
                fail(fail_context +
                     "Reading foreign config file failed with:\n%r" % (ex, ))
        else:
            fail(fail_context +
                 "Failed to find the repository configuration file!")

    # Process the imported repositories, in order
    for repo_entry in repos:
        if not isinstance(repo_entry, dict):
            fail(fail_context +
                 "Expected \"repos\" entries to be objects, but found:\n%r" %
                 (json.dumps(repo_entry, indent=2), ))
        repo_entry = cast(Json, repo_entry)

        core_repos = handle_import(archive_type,
                                   remote_stub,
                                   repo_entry,
                                   core_repos,
                                   foreign_config,
                                   fail_context=fail_context)

    # Clean up local fetch
    try_rmtree(to_clean_up)
    return core_repos


###
# Deduplication logic
##


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
        """Get equality condition between roots."""
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
        elif a["type"] == "computed":
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
        """Get description of a given root."""
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
        """Equality condition between repositories with respect to roots."""
        if name_a == name_b:
            return True
        root_a = None
        root_b = None
        for root_name in REPO_ROOTS:
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
            # Check equality between roots
            if not repo_roots_equal(repos, a, b):
                mark_as_different(a, b)
                continue
            # Check equality between bindings
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


def deduplicate(repos: Json, user_keep: List[str]) -> Json:
    """Deduplicate repository names in a configuration object,
    keeping the main repository and names provided explicitly by the user."""
    keep = set(user_keep)
    main = repos.get("main")
    if isinstance(main, str):
        keep.add(main)

    def choose_representative(c: List[str]) -> str:
        """Out of a bisimilarity class chose a main representative."""
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
        """Merge the pragma fields in a sensible manner."""
        desc = cast(Union[Any, Json], repos["repositories"][rep]["repository"])
        if not isinstance(desc, dict):
            return desc
        pragma = desc.get("pragma", {})
        # Clear pragma absent unless all merged repos that are not references
        # have the pragma
        absent: bool = pragma.get("absent", False)
        for c in merged:
            alt_desc = cast(Union[Any, Json],
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
            alt_desc = cast(Union[Any, Json],
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
        root: Any = repos["repositories"][name]["repository"]
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
            desc: Json = repos["repositories"][name]
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
                root_val = desc.get(root)
                if isinstance(root_val, str) and (root_val in renaming):
                    new_roots[root] = final_root_reference(root_val)
            desc = dict(desc, **new_roots)
            new_repos[name] = desc
    return dict(repos, **{"repositories": new_repos})


###
# Core logic
##


def lock_config(input_file: str) -> Json:
    """Main logic of just-lock."""
    input_config: Json = {}
    try:
        with open(input_file) as f:
            input_config = json.load(f)
    except OSError:
        fail("Failed to open input file %s" % (input_file, ))
    except Exception as ex:
        fail("Reading input file %s failed with:\n%r" % (input_file, ex))

    # Get the known fields
    main: Optional[str] = None
    if input_config.get("main") is not None:
        main = input_config.get("main")
        if not isinstance(main, str):
            fail("Expected field \"main\" to be a string, but found:\n%r" %
                 (json.dumps(main, indent=2), ))

    repositories: Json = input_config.get("repositories", DEFAULT_REPO)
    if not repositories:  # if empty, set it as default
        repositories = DEFAULT_REPO

    imports: List[Any] = input_config.get("imports", [])
    if not isinstance(imports, list):
        fail("Expected field \"imports\" to be a list, but found:\n%r" %
             (json.dumps(imports, indent=2), ))

    keep: List[Any] = input_config.get("keep", [])
    if not isinstance(keep, list):
        fail("Expected field \"keep\" to be a list, but found:\n%r" %
             (json.dumps(keep, indent=2), ))

    # Initialize the core config, which will be extended with imports
    core_config: Json = {}
    if main is not None:
        core_config = dict(core_config, **{"main": main})
    core_config = dict(core_config, **{"repositories": repositories})

    # Acquire garbage collector locks
    git_gc_lock = gc_repo_lock_acquire(is_shared=True)
    storage_gc_lock = gc_storage_lock_acquire(is_shared=True)

    # Handle imports
    for entry in imports:
        if not isinstance(entry, dict):
            fail("Expected import entries to be objects, but found:\n%r" %
                 (json.dumps(entry, indent=2), ))
        entry = cast(Json, entry)

        source = entry.get("source")
        if not isinstance(source, str):
            fail("Expected field \"source\" to be a string, but found:\n%r" %
                 (json.dumps(source, indent=2), ))

        if source == "git":
            core_config["repositories"] = import_from_git(
                core_config["repositories"], entry)
        elif source == "file":
            core_config["repositories"] = import_from_file(
                core_config["repositories"], entry)
        elif source == "archive":
            core_config["repositories"] = import_from_archive(
                core_config["repositories"], entry)
        elif source == "git-tree":
            # TODO(psarbu): Implement source "git-tree"
            warn("Import from source \"git-tree\" not yet implemented!")
        elif source == "generic":
            # TODO(psarbu): Implement source "generic"
            warn("Import from source \"generic\" not yet implemented!")
        else:
            fail("Unknown source for import entry \n%r" %
                 (json.dumps(entry, indent=2), ))

    # Release garbage collector locks
    lock_release(storage_gc_lock)
    lock_release(git_gc_lock)

    # Deduplicate output config
    core_config = deduplicate(core_config, keep)

    return core_config


def main():
    parser = ArgumentParser(
        prog="just-lock",
        description="Generate or update a multi-repository configuration file",
        exit_on_error=False,  # catch parsing errors ourselves
    )
    parser.add_argument("-C",
                        dest="input_file",
                        help="Input configuration file",
                        metavar="FILE")
    parser.add_argument("-o",
                        dest="output_file",
                        help="Output multi-repository configuration file",
                        metavar="FILE")
    parser.add_argument("--local-build-root",
                        dest="local_build_root",
                        help="Root for CAS, repository space, etc",
                        metavar="PATH")
    parser.add_argument("--git",
                        dest="git_bin",
                        help="Git binary to use",
                        metavar="PATH")
    parser.add_argument("--launcher",
                        dest="launcher",
                        help="Local launcher to use for commands in imports",
                        metavar="JSON")

    try:
        args = parser.parse_args()
    except ArgumentError as err:
        fail("Failed parsing command line arguments with:\n%s" %
             (err.message, ))

    # Get the path of the input file
    if args.input_file:
        input_file = cast(str, args.input_file)
        # if explicitly provided, it must be an existing file
        if not os.path.isfile(input_file):
            fail("Provided input path '%s' is not a file!" % (input_file, ))
    else:
        # search usual paths in workspace root
        input_file = get_repository_config_file(DEFAULT_INPUT_CONFIG_NAME)
        if not input_file:
            fail("Failed to find an input file in the workspace")

    # Get the path of the output file
    if args.output_file:
        output_file = cast(str, args.output_file)
    else:
        # search usual paths in workspace root
        output_file = get_repository_config_file(DEFAULT_JUSTMR_CONFIG_NAME)
        if not output_file:
            # set it next to the input file
            parent_path = Path(input_file).parent
            output_file = os.path.realpath(
                os.path.join(parent_path, DEFAULT_JUSTMR_CONFIG_NAME))

    # Process the rest of the command line; use globals for simplicity
    global g_ROOT, g_GIT, g_LAUNCHER
    if args.local_build_root:
        g_ROOT = os.path.abspath(args.local_build_root)
    if args.git_bin:
        g_GIT = args.git_bin
    if args.launcher:
        g_LAUNCHER = json.loads(args.launcher)

    out_config = lock_config(input_file)
    with open(output_file, "w") as f:
        json.dump(out_config, f, indent=2)


if __name__ == "__main__":
    main()
