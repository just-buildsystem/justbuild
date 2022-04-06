#!/usr/bin/env python3

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile


from pathlib import Path

# path within the repository (constants)

REPOS = "etc/repos.json"
BOOTSTRAP_CC = ["clang++", "-std=c++20", "-DBOOTSTRAP_BUILD_TOOL"]
MAIN_MODULE = ""
MAIN_TARGET = ""
MAIN_STAGE = "bin/just"

# relevant directories (global variables)

SRCDIR = os.getcwd()
WRKDIR = None
DISTDIR = []

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

def setup_deps():
    # unpack all dependencies and return a list of
    # additional C++ flags required
    with open(os.path.join(SRCDIR, REPOS)) as f:
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
                print("Checksum mismatch for %r. Expected %r, found %r"
                      % (archive, expected_checksum, actual_checksum))
            print("Unpacking %r from %r" % (repo, archive))
            unpack_location = os.path.join(WRKDIR, "deps", repo)
            os.makedirs(unpack_location)
            if desc["type"] == "zip":
                subprocess.run(["unzip", "-d", ".", archive],
                               cwd=unpack_location, stdout=subprocess.DEVNULL)
            else:
                subprocess.run(["tar", "xf", archive],
                               cwd=unpack_location)
            subdir = os.path.join(unpack_location,
                                  desc.get("subdir", "."))
            include_dir = os.path.join(subdir,
                                       hints.get("include_dir", "."))
            include_name = hints.get("include_name", repo)
            os.symlink(os.path.normpath(include_dir),
                       os.path.join(include_location, include_name))
            if "build" in hints:
                run(["sh", "-c", hints["build"]], cwd=subdir)
            if "link" in hints:
                link_flags.extend(["-L", subdir])
        if "link" in hints:
            link_flags.extend(hints["link"])

    return {
        "include": ["-I", include_location],
        "link": link_flags
    }

def bootstrap():
    # TODO: add package build mode, building against preinstalled dependencies
    # rather than building dependencies ourselves.
    print("Bootstrapping in %r from sources %r, taking files from %r"
          % (WRKDIR, SRCDIR, DISTDIR))
    os.makedirs(WRKDIR, exist_ok=True)
    dep_flags = setup_deps();
    # handle proto
    src_wrkdir = os.path.join(WRKDIR, "src")
    shutil.copytree(SRCDIR, src_wrkdir)
    flags = ["-I", src_wrkdir] + dep_flags["include"]
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
        obj_file_name =f[:-len(".cpp")] + ".o"
        object_files.append(obj_file_name)
        cmd = BOOTSTRAP_CC + flags + ["-c", f, "-o", obj_file_name]
        run(cmd, cwd=src_wrkdir)
    bootstrap_just = os.path.join(WRKDIR, "bootstrap-just")
    cmd = BOOTSTRAP_CC + ["-o", bootstrap_just] + object_files + dep_flags["link"]
    run(cmd, cwd=src_wrkdir)
    CONF_FILE = os.path.join(WRKDIR, "repo-conf.json")
    LOCAL_ROOT = os.path.join(WRKDIR, ".just")
    os.makedirs(LOCAL_ROOT, exist_ok=True)
    run(["sh", "-c",
         "cp `./bin/just-mr.py --always_file -C %s --local_build_root=%s setup just` %s"
         % (REPOS, LOCAL_ROOT, CONF_FILE)],
        cwd=src_wrkdir)
    GRAPH = os.path.join(WRKDIR, "graph.json")
    TO_BUILD = os.path.join(WRKDIR, "to_build.json")
    run([bootstrap_just, "analyse",
         "-C", CONF_FILE,
         "--dump_graph", GRAPH,
         "--dump_artifacts_to_build", TO_BUILD,
         MAIN_MODULE, MAIN_TARGET],
        cwd=src_wrkdir)
    run(["./bin/bootstrap-traverser.py",
         "-C", CONF_FILE,
         "--default_workspace", src_wrkdir,
         GRAPH, TO_BUILD],
        cwd=src_wrkdir)
    OUT = os.path.join(WRKDIR, "out")
    run(["./out-boot/%s" % (MAIN_STAGE,),
         "install", "-C", CONF_FILE,
         "-o", OUT, "--local_build_root", LOCAL_ROOT,
         MAIN_MODULE, MAIN_TARGET],
        cwd=src_wrkdir)



def main(args):
    global SRCDIR
    global WRKDIR
    global DISTDIR
    if len(args) > 1:
        SRCDIR = os.path.abspath(args[1])
    if len(args) > 2:
        WRKDIR = os.path.abspath(args[2])

    if not WRKDIR:
        WRKDIR = tempfile.mkdtemp()
    if not DISTDIR:
        DISTDIR = [os.path.join(Path.home(), ".distfiles")]
    bootstrap()

if __name__ == "__main__":
    # Parse options, set DISTDIR
    main(sys.argv)
