#!/usr/bin/env python3

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

from argparse import ArgumentParser
from pathlib import Path

JUST = "just"
ROOT = "/justroot"
DISTDIR = []

ALWAYS_FILE = False

GIT_CHECKOUT_LOCATIONS = {}

TAKE_OVER = [
    "bindings",
    "target_file_name",
    "index_file_name",
    "rule_file_name",
    "expression_file_name",
]
ALT_DIRS = [
    "target_root",
    "rule_root",
    "expression_root",
    "index_root",
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


def log(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)


def fail(s):
    log(s)
    sys.exit(1)


def run_cmd(cmd, *, env=None, stdout=subprocess.DEVNULL, stdin=None, cwd):
    result = subprocess.run(cmd, cwd=cwd, env=env, stdout=stdout, stdin=stdin)
    if result.returncode != 0:
        fail("Command %s in %s failed" % (cmd, cwd))


def read_config(configfile):
    if configfile:
        with open(configfile) as f:
            return json.load(f)
    default_config = os.path.join(Path.home(), ".just-repos.json")

    if os.path.isfile(default_config):
        with open(default_config) as f:
            return json.load(f)

    return {}


def git_root(*, upstream):
    if upstream in GIT_CHECKOUT_LOCATIONS:
        return GIT_CHECKOUT_LOCATIONS[upstream]
    elif upstream and os.path.isabs(upstream):
        return upstream
    else:
        return os.path.join(ROOT, "git")


def git_keep(commit, *, upstream):
    if upstream in GIT_CHECKOUT_LOCATIONS:
        # for those, we assume the referenced commit is kept by
        # some branch anyway
        return
    run_cmd(
        [
            "git", "tag", "-f", "-m", "Keep referenced tree alive",
            "keep-%s" % (commit, ), commit
        ],
        cwd=git_root(upstream=upstream),
        env=dict(os.environ, **GIT_NOBODY_ENV),
    )


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
    cas_root = os.path.join(ROOT, "protocol-dependent/git-sha1/casf")
    basename = git_hash(data)
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
    return os.path.join(ROOT, "protocol-dependent/git-sha1/casf", h)


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
    return os.path.join(ROOT, "tmp-workspaces", repo_type, content)


def archive_tree_id_file(content, repo_type):
    return os.path.join(ROOT, "tree-map", repo_type, content)


def archive_checkout(desc, repo_type="archive", *, fetch_only=False):
    content_id = desc["content"]
    target = archive_checkout_dir(content_id, repo_type=repo_type)
    if ALWAYS_FILE and os.path.exists(target):
        return ["file", subdir_path(target, desc)]
    tree_id_file = archive_tree_id_file(content_id, repo_type=repo_type)
    if (not ALWAYS_FILE) and os.path.exists(tree_id_file):
        with open(tree_id_file) as f:
            archive_tree_id = f.read()
        return [
            "git tree",
            git_subtree(tree=archive_tree_id,
                        subdir=desc.get("subdir", "."),
                        upstream=None),
            git_root(upstream=None),
        ]
    if not is_in_cas(content_id):
        distfile = desc.get("distfile")
        if not distfile:
            distfile = os.path.basename(desc.get("fetch"))
        if distfile:
            add_distfile_to_cas(distfile)
    if not is_in_cas(content_id):
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
        if not is_in_cas(content_id):
            fail("Failed to fetch a file with id %s from %s" %
                 (content_id, url))
    if fetch_only:
        return
    if not ALWAYS_FILE:
        target = archive_tmp_checkout_dir(content_id, repo_type=repo_type)
    os.makedirs(target)
    if repo_type == "zip":
        run_cmd(["unzip", "-d", ".", cas_path(content_id)], cwd=target)
    else:
        run_cmd(["tar", "xf", cas_path(content_id)], cwd=target)
    if ALWAYS_FILE:
        return ["file", subdir_path(target, desc)]
    tree = import_to_git(target, repo_type, content_id)
    shutil.rmtree(target)
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
        ["git", "add", "."],
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
        target = archive_tmp_checkout_dir(os.path.relpath(fpath, "/"),
                                          repo_type="file")
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copytree(fpath, target)
        tree = import_to_git(target, "file", fpath)
        shutil.rmtree(target)
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


def checkout(desc, *, name, repos):
    repo_desc = resolve_repo(desc, repos=repos)
    repo_type = repo_desc.get("type")
    if repo_type == "git":
        return git_checkout(repo_desc)
    if repo_type in ["archive", "zip"]:
        return archive_checkout(repo_desc, repo_type=repo_type)
    if repo_type == "file":
        return file_checkout(repo_desc)
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


def setup(*, config, args, interactive=False):
    repos = config.get("repositories", {})
    repos_to_setup = repos.keys()
    repos_to_include = repos.keys()
    mr_config = {}
    main = None

    if args:
        if len(args) > 1:
            fail("Usage: %s setup [<main repo>]" % (sys.argv[0], ))
        main = args[0]
        repos_to_include, repos_to_setup = reachable_repositories(main,
                                                                  repos=repos)
        mr_config["main"] = main

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

    return add_to_cas(json.dumps(mr_config, indent=2, sort_keys=True))


def build(*, config, args):
    if len(args) != 3:
        fail("Usage: %s build <repo> <moudle> <target>" % (sys.argv[0], ))
    config = setup(config=config, args=[args[0]])
    cmd = [
        JUST, "build", "-C", config, "--local-build-root", ROOT, args[1],
        args[2]
    ]
    log("Setup finished, exec %s" % (cmd, ))
    os.execvp(JUST, cmd)


def install(*, config, args):
    if len(args) != 4:
        fail("Usage: %s install <repo> <moudle> <target> <install-path>" %
             (sys.argv[0], ))
    config = setup(config=config, args=[args[0]])
    cmd = [
        JUST, "install", "-C", config, "--local-build-root", ROOT, "-o",
        args[3], args[1], args[2]
    ]
    log("Setup finished, exec %s" % (cmd, ))
    os.execvp(JUST, cmd)


def update(*, config, args):
    for repo in args:
        desc = config["repositories"][repo]["repository"]
        desc = resolve_repo(desc, repos=config["repositories"])
        repo_type = desc.get("type")
        if repo_type == "git":
            update_git(desc)
        else:
            fail("Don't know how to update %s repositories" % (repo_type, ))
    print(json.dumps(config, indent=2))
    sys.exit(0)


def fetch(*, config, args):
    if args:
        print("Warning: ignoring arguments %r" % (args, ))
    fetch_dir = None
    for d in DISTDIR:
        if os.path.isdir(d):
            fetch_dir = os.path.abspath(d)
            break
    if not fetch_dir:
        print("No directory found to fetch to, considered %r" % (DISTDIR, ))
        sys.exit(1)
    print("Fetching to %r" % (fetch_dir, ))

    repos = config["repositories"]
    for repo, desc in repos.items():
        if ("repository" in desc and isinstance(desc["repository"], dict)
                and desc["repository"]["type"] in ["zip", "archive"]):
            repo_desc = desc["repository"]
            distfile = repo_desc.get("distfile") or os.path.basename(
                repo_desc["fetch"])
            content = repo_desc["content"]
            print("%r --> %r (content: %s)" % (repo, distfile, content))
            archive_checkout(repo_desc, repo_desc["type"], fetch_only=True)
            shutil.copyfile(cas_path(content),
                            os.path.join(fetch_dir, distfile))

    sys.exit(0)


def main():
    parser = ArgumentParser()
    parser.add_argument("-C",
                        dest="repository_config",
                        help="Repository-description file to use",
                        metavar="FILE")
    parser.add_argument("-L",
                        dest="checkout_location",
                        help="Specification file for checkout locations")
    parser.add_argument("--local-build-root",
                        dest="local_build_root",
                        help="Root for CAS, repository space, etc",
                        metavar="PATH")
    parser.add_argument("--distdir",
                        dest="distdir",
                        action="append",
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

    (options, args) = parser.parse_known_args()
    config = read_config(options.repository_config)
    global ROOT
    ROOT = options.local_build_root or os.path.join(Path.home(), ".cache/just")
    ROOT = ROOT if os.path.isabs(ROOT) else os.path.abspath(ROOT)
    global GIT_CHECKOUT_LOCATIONS
    if options.checkout_location:
        with open(options.checkout_location) as f:
            GIT_CHECKOUT_LOCATIONS = json.load(f).get("checkouts",
                                                      {}).get("git", {})
    elif os.path.isfile(os.path.join(Path().home(), ".just-local.json")):
        with open(os.path.join(Path().home(), ".just-local.json")) as f:
            GIT_CHECKOUT_LOCATIONS = json.load(f).get("checkouts",
                                                      {}).get("git", {})
    global DISTDIR
    if options.distdir:
        DISTDIR = options.distdir

    DISTDIR.append(os.path.join(Path.home(), ".distfiles"))

    global JUST
    if options.just:
        JUST = os.path.abspath(options.just)

    global ALWAYS_FILE
    ALWAYS_FILE = options.always_file

    if not args:
        fail("Usage: %s <cmd> [<args>]" % (sys.argv[0], ))
    if args[0] == "setup":
        # Setup for interactive use, i.e., fetch the required repositories
        # and generate an appropriate multi-repository configuration file.
        # Store it in the CAS and print its path on stdout.
        #
        # For the main repository (if specified), leave out the workspace
        # so that in the usage of just the workspace is determined from
        # the working directory; in this way, working on a checkout of that
        # repository is possible, while having all dependencies set up
        # correctly.
        print(setup(config=config, args=args[1:], interactive=True))
        return
    if args[0] == "build":
        build(config=config, args=args[1:])
    if args[0] == "install":
        install(config=config, args=args[1:])
    if args[0] == "update":
        update(config=config, args=args[1:])
    if args[0] == "fetch":
        fetch(config=config, args=args[1:])
    fail("Unknown subcommand %s" % (args[0], ))


if __name__ == "__main__":
    main()
