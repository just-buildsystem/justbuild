#!/usr/bin/env python3

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import platform

from pathlib import Path

# path within the repository (constants)

REPOS = "etc/repos.json"
BOOTSTRAP_CC = ["clang++", "-std=c++20", "-DBOOTSTRAP_BUILD_TOOL"]
MAIN_MODULE = ""
MAIN_TARGET = ""
MAIN_STAGE = "bin/just"

LOCAL_LINK_DIRS_MODULE = "src/buildtool/main"
LOCAL_LINK_DIRS_TARGET = "just"

# architecture related configuration (global variables)
ARCHS = {
  'i686':'x86',
  'x86_64':'x86_64',
  'arm':'arm',
  'aarch64':'arm64'
}
MACH = platform.machine()
CONF = ('{"ARCH":"%s"}' % (ARCHS[MACH],)) if MACH in ARCHS else '{}'

# relevant directories (global variables)

SRCDIR = os.getcwd()
WRKDIR = None
DISTDIR = []
LOCALBASE = "/"

# other global variables

LOCAL_DEPS = False

def git_hash(content):
    header = "blob {}\0".format(len(content)).encode('utf-8')
    h = hashlib.sha1()
    h.update(header)
    h.update(content)
    return h.hexdigest()


def get_checksum(filename):
    with open(filename, "rb") as f:
        data = f.read()
    return git_hash(data)


def get_archive(*, distfile, fetch):
    # Fetch the archive, if necessary. Return path to archive
    for d in DISTDIR:
        candidate_path = os.path.join(d, distfile)
        if os.path.isfile(candidate_path):
            return candidate_path
    # Fetch to bootstrap working directory
    fetch_dir = os.path.join(WRKDIR, "fetch")
    os.makedirs(fetch_dir, exist_ok=True)
    target = os.path.join(fetch_dir, distfile)
    subprocess.run(["wget", "-O", target, fetch])
    return target


def run(cmd, *, cwd, **kwargs):
    print("Running %r in %r" % (cmd, cwd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True, **kwargs)


def setup_deps(src_wrkdir):
    # unpack all dependencies and return a list of
    # additional C++ flags required
    with open(os.path.join(src_wrkdir, REPOS)) as f:
        config = json.load(f)["repositories"]
    include_location = os.path.join(WRKDIR, "dep_includes")
    link_flags = []
    os.makedirs(include_location)
    for repo, total_desc in config.items():
        desc = total_desc.get("repository", {})
        if not isinstance(desc, dict):
            # Indirect definition; we will set up the repository at the
            # resolved place, which also has to be part of the global
            # repository description.
            continue
        hints = total_desc.get("bootstrap", {})
        if desc.get("type") in ["archive", "zip"]:
            fetch = desc["fetch"]
            distfile = desc.get("distfile") or os.path.basename(fetch)
            archive = get_archive(distfile=distfile, fetch=fetch)
            actual_checksum = get_checksum(archive)
            expected_checksum = desc.get("content")
            if actual_checksum != expected_checksum:
                print("Checksum mismatch for %r. Expected %r, found %r" %
                      (archive, expected_checksum, actual_checksum))
            print("Unpacking %r from %r" % (repo, archive))
            unpack_location = os.path.join(WRKDIR, "deps", repo)
            os.makedirs(unpack_location)
            if desc["type"] == "zip":
                subprocess.run(["unzip", "-d", ".", archive],
                               cwd=unpack_location,
                               stdout=subprocess.DEVNULL)
            else:
                subprocess.run(["tar", "xf", archive], cwd=unpack_location)
            subdir = os.path.join(unpack_location, desc.get("subdir", "."))
            include_dir = os.path.join(subdir, hints.get("include_dir", "."))
            include_name = hints.get("include_name", repo)
            if include_name == ".":
                for entry in os.listdir(include_dir):
                    os.symlink(
                        os.path.normpath(os.path.join(include_dir, entry)),
                        os.path.join(include_location, entry))
            else:
                os.symlink(os.path.normpath(include_dir),
                           os.path.join(include_location, include_name))
            if "build" in hints:
                run(["sh", "-c", hints["build"]], cwd=subdir)
            if "link" in hints:
                link_flags.extend(["-L", subdir])
        if "link" in hints:
            link_flags.extend(hints["link"])

    return {"include": ["-I", include_location], "link": link_flags}

def config_to_local(*, repos_file, link_targets_file):
    with open(repos_file) as f:
        repos = json.load(f)
    global_link_dirs = set()
    for repo in repos["repositories"]:
        desc = repos["repositories"][repo]
        repo_desc = desc.get("repository")
        if not isinstance(repo_desc, dict):
            repo_desc = {}
        if repo_desc.get("type") in ["archive", "zip"]:
            local_bootstrap = desc.get("local_bootstrap", {})
            desc["repository"] = {
                "type": "file",
                "path": os.path.normpath(
                    os.path.join(
                        LOCALBASE,
                        local_bootstrap.get("local_path", ".")))
            }
            if "link_dirs" in local_bootstrap:
                link = []
                for entry in local_bootstrap["link_dirs"]:
                    link += ["-L", os.path.join(LOCALBASE, entry)]
                    global_link_dirs.add(entry)
                link += local_bootstrap.get("link", [])
                local_bootstrap["link"] = link
            desc["bootstrap"] = local_bootstrap
            if "local_bootstrap" in desc:
                del desc["local_bootstrap"]
        if repo_desc.get("type") == "file":
            local_bootstrap = desc.get("local_bootstrap", {})
            desc["repository"] = {
                "type": "file",
                "path": local_bootstrap.get("local_path", desc["repository"].get("path"))
            }
            desc["bootstrap"] = local_bootstrap
            if "local_bootstrap" in desc:
                del desc["local_bootstrap"]

    print("just-mr config rewritten to local:\n%s\n"
          % (json.dumps(repos, indent=2)))
    os.unlink(repos_file)
    with open(repos_file, "w") as f:
        json.dump(repos, f, indent=2)

    with open(link_targets_file) as f:
        target = json.load(f)
    main = target[LOCAL_LINK_DIRS_TARGET]
    link_external = ["-L%s" % (os.path.join(LOCALBASE, d),)
                     for d in global_link_dirs]
    print("External link arguments %r" % (link_external,))
    main["link external"] = link_external
    target[LOCAL_LINK_DIRS_TARGET] = main
    os.unlink(link_targets_file)
    with open(link_targets_file, "w") as f:
        json.dump(target, f, indent=2)

def bootstrap():
    if LOCAL_DEPS:
        print("Bootstrap build in %r from sources %r against LOCALBASE %r"
              % (WRKDIR, SRCDIR, LOCALBASE))
    else:
        print("Bootstrapping in %r from sources %r, taking files from %r" %
              (WRKDIR, SRCDIR, DISTDIR))
    os.makedirs(WRKDIR, exist_ok=True)
    src_wrkdir = os.path.join(WRKDIR, "src")
    shutil.copytree(SRCDIR, src_wrkdir)
    if LOCAL_DEPS:
        config_to_local(
            repos_file =os.path.join(src_wrkdir, REPOS),
            link_targets_file=os.path.join(src_wrkdir, LOCAL_LINK_DIRS_MODULE, "TARGETS")
        )
    dep_flags = setup_deps(src_wrkdir)
    # handle proto
    flags = ["-I", src_wrkdir] + dep_flags["include"] + ["-I", os.path.join(LOCALBASE, "include")]
    cpp_files = []
    for root, dirs, files in os.walk(src_wrkdir):
        if 'test' in dirs:
            dirs.remove('test')
        if 'execution_api' in dirs:
            dirs.remove('execution_api')
        for f in files:
            if f.endswith(".cpp"):
                cpp_files.append(os.path.join(root, f))
    object_files = []
    for f in cpp_files:
        obj_file_name = f[:-len(".cpp")] + ".o"
        object_files.append(obj_file_name)
        cmd = BOOTSTRAP_CC + flags + ["-c", f, "-o", obj_file_name]
        run(cmd, cwd=src_wrkdir)
    bootstrap_just = os.path.join(WRKDIR, "bootstrap-just")
    cmd = BOOTSTRAP_CC + ["-o", bootstrap_just
                          ] + object_files + dep_flags["link"]
    run(cmd, cwd=src_wrkdir)
    CONF_FILE = os.path.join(WRKDIR, "repo-conf.json")
    LOCAL_ROOT = os.path.join(WRKDIR, ".just")
    os.makedirs(LOCAL_ROOT, exist_ok=True)
    distdirs = " --distdir=".join(DISTDIR)
    run([
        "sh", "-c",
        "cp `./bin/just-mr.py --always-file -C %s --local-build-root=%s --distdir=%s setup just` %s"
        % (REPOS, LOCAL_ROOT, distdirs, CONF_FILE)
    ],
        cwd=src_wrkdir)
    GRAPH = os.path.join(WRKDIR, "graph.json")
    TO_BUILD = os.path.join(WRKDIR, "to_build.json")
    run([
        bootstrap_just, "analyse", "-C", CONF_FILE, "-D", CONF, "--dump-graph", GRAPH,
        "--dump-artifacts-to-build", TO_BUILD, MAIN_MODULE, MAIN_TARGET
    ],
        cwd=src_wrkdir)
    run([
        "./bin/bootstrap-traverser.py", "-C", CONF_FILE, "--default-workspace",
        src_wrkdir, GRAPH, TO_BUILD
    ],
        cwd=src_wrkdir)
    OUT = os.path.join(WRKDIR, "out")
    run([
        "./out-boot/%s" % (MAIN_STAGE, ), "install", "-C", CONF_FILE, "-D", CONF, "-o", OUT,
        "--local-build-root", LOCAL_ROOT, MAIN_MODULE, MAIN_TARGET
    ],
        cwd=src_wrkdir)


def main(args):
    global SRCDIR
    global WRKDIR
    global DISTDIR
    global LOCAL_DEPS
    global LOCALBASE
    if len(args) > 1:
        SRCDIR = os.path.abspath(args[1])
    if len(args) > 2:
        WRKDIR = os.path.abspath(args[2])
    if len(args) > 3:
        DISTDIR = [os.path.abspath(p) for p in args[3:]]

    if not WRKDIR:
        WRKDIR = tempfile.mkdtemp()
    if not DISTDIR:
        DISTDIR = [os.path.join(Path.home(), ".distfiles")]

    LOCAL_DEPS = "PACKAGE" in os.environ
    LOCALBASE = os.environ.get("LOCALBASE", "/")
    bootstrap()


if __name__ == "__main__":
    # Parse options, set DISTDIR
    main(sys.argv)
