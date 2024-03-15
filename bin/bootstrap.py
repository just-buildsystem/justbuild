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
from itertools import chain
import json
import os
import shutil
import subprocess
import sys
import tempfile
import platform
import logging

from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

from typing import Any, Callable, Dict, List, Optional, Set, cast

# generic JSON type that avoids getter issues; proper use is being enforced by
# return types of methods and typing vars holding return values of json getters
Json = Dict[str, Any]

# path within the repository (constants)

DEBUG = os.environ.get("DEBUG")

REPOS: str = "etc/repos.json"
MAIN_MODULE: str = ""
MAIN_TARGET: str = ""
MAIN_STAGE: str = "bin/just"

LOCAL_LINK_DIRS_MODULE: str = "src/buildtool/main"
LOCAL_LINK_DIRS_TARGET: str = "just"

# architecture related configuration (global variables)
g_CONF: Json = {}
if 'JUST_BUILD_CONF' in os.environ:
    g_CONF = json.loads(os.environ['JUST_BUILD_CONF'])

if "PACKAGE" in os.environ:
    g_CONF["ADD_CFLAGS"] = ["-Wno-error", "-Wno-pedantic"] + g_CONF.get(
        "ADD_CFLAGS", [])
    g_CONF["ADD_CXXFLAGS"] = ["-Wno-error", "-Wno-pedantic"] + g_CONF.get(
        "ADD_CXXFLAGS", [])

ARCHS: Dict[str, str] = {
    'i686': 'x86',
    'x86_64': 'x86_64',
    'arm': 'arm',
    'aarch64': 'arm64'
}
if "OS" not in g_CONF:
    g_CONF["OS"] = platform.system().lower()
if "ARCH" not in g_CONF:
    MACH = platform.machine()
    if MACH in ARCHS:
        g_CONF["ARCH"] = ARCHS[MACH]
    elif MACH in ARCHS.values():
        g_CONF["ARCH"] = MACH
    else:
        expected = ", ".join(chain(ARCHS.keys(), ARCHS.values()))
        logging.error(f"Couldn't setup ARCH. Found {MACH} expected one of {expected}")

if 'SOURCE_DATE_EPOCH' in os.environ:
    g_CONF['SOURCE_DATE_EPOCH'] = int(os.environ['SOURCE_DATE_EPOCH'])

g_CONFIG_PATHS: List[str] = []
if 'PKG_CONFIG_PATH' in os.environ:
    g_CONFIG_PATHS += [os.environ['PKG_CONFIG_PATH']]

ENV: Dict[str, str] = g_CONF.setdefault("ENV", {})
if 'PKG_CONFIG_PATH' in ENV:
    g_CONFIG_PATHS += [ENV['PKG_CONFIG_PATH']]

g_LOCALBASE: str = "/"
if 'LOCALBASE' in os.environ:
    g_LOCALBASE = os.environ['LOCALBASE']
    pkg_paths = ['lib/pkgconfig', 'share/pkgconfig']
    if 'PKG_PATHS' in os.environ:
        pkg_paths = json.loads(os.environ['PKG_PATHS'])
    g_CONFIG_PATHS += [os.path.join(g_LOCALBASE, p) for p in pkg_paths]

ENV['PKG_CONFIG_PATH'] = ":".join(g_CONFIG_PATHS)

CONF_STRING: str = json.dumps(g_CONF)

OS: str = g_CONF["OS"]
ARCH: str = g_CONF["ARCH"]
g_AR: str = "ar"
g_CC: str = "cc"
g_CXX: str = "c++"
g_CFLAGS: List[str] = []
g_CXXFLAGS: List[str] = []
g_FINAL_LDFLAGS: List[str] = ["-Wl,-z,stack-size=8388608"]

if "TOOLCHAIN_CONFIG" in g_CONF and "FAMILY" in g_CONF["TOOLCHAIN_CONFIG"]:
    if g_CONF["TOOLCHAIN_CONFIG"]["FAMILY"] == "gnu":
        g_CC = "gcc"
        g_CXX = "g++"
    elif g_CONF["TOOLCHAIN_CONFIG"]["FAMILY"] == "clang":
        g_CC = "clang"
        g_CXX = "clang++"

if "AR" in g_CONF:
    g_AR = g_CONF["AR"]
if "CC" in g_CONF:
    g_CC = g_CONF["CC"]
if "CXX" in g_CONF:
    g_CXX = g_CONF["CXX"]
if "ADD_CFLAGS" in g_CONF:
    g_CFLAGS = g_CONF["ADD_CFLAGS"]
if "ADD_CXXFLAGS" in g_CONF:
    g_CXXFLAGS = g_CONF["ADD_CXXFLAGS"]
if "FINAL_LDFLAGS" in g_CONF:
    g_FINAL_LDFLAGS += g_CONF["FINAL_LDFLAGS"]

BOOTSTRAP_CC: List[str] = [g_CXX] + g_CXXFLAGS + [
    "-std=c++20", "-DBOOTSTRAP_BUILD_TOOL"
]

# relevant directories (global variables)

g_SRCDIR: str = os.getcwd()
g_WRKDIR: Optional[str] = None
g_DISTDIR: List[str] = []
g_NON_LOCAL_DEPS: List[str] = []

# other global variables

g_LOCAL_DEPS: bool = False


def git_hash(content: bytes) -> str:
    header = "blob {}\0".format(len(content)).encode('utf-8')
    h = hashlib.sha1()
    h.update(header)
    h.update(content)
    return h.hexdigest()


def get_checksum(filename: str) -> str:
    with open(filename, "rb") as f:
        data = f.read()
    return git_hash(data)


def get_archive(*, distfile: str, fetch: str) -> str:
    # Fetch the archive, if necessary. Return path to archive
    for d in g_DISTDIR:
        candidate_path = os.path.join(d, distfile)
        if os.path.isfile(candidate_path):
            return candidate_path
    # Fetch to bootstrap working directory
    fetch_dir: str = os.path.join(cast(str, g_WRKDIR), "fetch")
    os.makedirs(fetch_dir, exist_ok=True)
    target: str = os.path.join(fetch_dir, distfile)
    subprocess.run(["wget", "-O", target, fetch])
    return target


def quote(args: List[str]) -> str:
    return ' '.join(["'" + arg.replace("'", "'\\''") + "'" for arg in args])


def run(cmd: List[str], *, cwd: str, **kwargs: Any) -> None:
    print("Running %r in %r" % (cmd, cwd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True, **kwargs)


def setup_deps(src_wrkdir: str) -> Json:
    # unpack all dependencies and return a list of
    # additional C++ flags required
    with open(os.path.join(src_wrkdir, REPOS)) as f:
        config = json.load(f)["repositories"]
    include_location: str = os.path.join(cast(str, g_WRKDIR), "dep_includes")
    link_flags: List[str] = []
    os.makedirs(include_location)
    for repo, total_desc in config.items():
        desc: Optional[Json] = total_desc.get("repository", {})
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
            unpack_location: str = os.path.join(cast(str, g_WRKDIR), "deps",
                                                repo)
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
            os_map = hints.get("os_map", dict())
            arch_map = hints.get("arch_map", dict())
            if "build" in hints:
                run([
                    "sh", "-c", hints["build"].format(
                        os=os_map.get(OS, OS),
                        arch=arch_map.get(ARCH, ARCH),
                        cc=g_CC,
                        cxx=g_CXX,
                        ar=g_AR,
                        cflags=quote(g_CFLAGS),
                        cxxflags=quote(g_CXXFLAGS),
                    )
                ],
                    cwd=subdir)
            if "link" in hints:
                link_flags.extend(["-L", subdir])
        if "link" in hints:
            link_flags.extend(hints["link"])

    return {"include": ["-I", include_location], "link": link_flags}


def config_to_local(*, repos_file: str, link_targets_file: str) -> None:
    with open(repos_file) as f:
        repos = json.load(f)
    global_link_dirs: Set[str] = set()
    changed_file_roots: Dict[str, str] = {}
    backup_layers: Json = {}
    for repo in repos["repositories"]:
        if repo in g_NON_LOCAL_DEPS:
            continue
        desc = repos["repositories"][repo]
        repo_desc: Optional[Json] = desc.get("repository")
        if not isinstance(repo_desc, dict):
            repo_desc = {}
        if repo_desc.get("type") in ["archive", "zip"]:
            pkg_bootstrap: Json = desc.get("pkg_bootstrap", {})
            desc["repository"] = {
                "type":
                "file",
                "path":
                os.path.normpath(
                    os.path.join(g_LOCALBASE,
                                 pkg_bootstrap.get("local_path", ".")))
            }
            if "link_dirs" in pkg_bootstrap:
                link: List[str] = []
                for entry in pkg_bootstrap["link_dirs"]:
                    link += ["-L", os.path.join(g_LOCALBASE, entry)]
                    global_link_dirs.add(entry)
                link += pkg_bootstrap.get("link", [])
                pkg_bootstrap["link"] = link
            desc["bootstrap"] = pkg_bootstrap
            if "pkg_bootstrap" in desc:
                del desc["pkg_bootstrap"]
        if repo_desc.get("type") == "file":
            pkg_bootstrap = desc.get("pkg_bootstrap", {})
            if pkg_bootstrap.get("local_path") and g_NON_LOCAL_DEPS:
                # local layer gets changed, keep a copy
                backup_name: str = "ORIGINAL: " + repo
                backup_layers[backup_name] = {
                    "repository": {
                        "type": "file",
                        "path": repo_desc.get("path")
                    }
                }
                changed_file_roots[repo] = backup_name
            desc["repository"] = {
                "type":
                "file",
                "path":
                pkg_bootstrap.get("local_path", desc["repository"].get("path"))
            }
            desc["bootstrap"] = pkg_bootstrap
            if "pkg_bootstrap" in desc:
                del desc["pkg_bootstrap"]

    # For repos that we didn't change to local, make file roots point
    # to the original version, so that, in particular, the original
    # target root will be used.
    for repo in g_NON_LOCAL_DEPS:
        for layer in ["target_root", "rule_root", "expression_root"]:
            layer_ref: str = repos["repositories"][repo].get(layer)
            if layer_ref in changed_file_roots:
                repos["repositories"][repo][layer] = changed_file_roots[
                    layer_ref]

    repos["repositories"] = dict(repos["repositories"], **backup_layers)

    print("just-mr config rewritten to local:\n%s\n" %
          (json.dumps(repos, indent=2)))
    os.unlink(repos_file)
    with open(repos_file, "w") as f:
        json.dump(repos, f, indent=2)

    with open(link_targets_file) as f:
        target = json.load(f)
    main = target[LOCAL_LINK_DIRS_TARGET]
    link_external = [
        "-L%s" % (os.path.join(g_LOCALBASE, d), ) for d in global_link_dirs
    ]
    print("External link arguments %r" % (link_external, ))
    main["private-ldflags"] = link_external
    target[LOCAL_LINK_DIRS_TARGET] = main
    os.unlink(link_targets_file)
    with open(link_targets_file, "w") as f:
        json.dump(target, f, indent=2)


def prune_config(*, repos_file: str, empty_dir: str) -> None:
    with open(repos_file) as f:
        repos = json.load(f)
    for repo in repos["repositories"]:
        if repo in g_NON_LOCAL_DEPS:
            continue
        desc = repos["repositories"][repo]
        if desc.get("bootstrap", {}).get("drop"):
            desc["repository"] = {"type": "file", "path": empty_dir}
    os.unlink(repos_file)
    with open(repos_file, "w") as f:
        json.dump(repos, f, indent=2)

def ignore_dst(dst: str) -> Callable[[str, List[str]], List[str]]:
    def ignore_(path: str, names: List[str]) -> List[str]:
        if os.path.normpath(path) == dst:
            return names
        for n in names:
            if os.path.normpath(os.path.join(path, n)) == dst:
                return[n]
        return []
    return ignore_


def copy_roots(*, repos_file: str, copy_dir: str) -> None:
    with open(repos_file) as f:
        repos = json.load(f)
    for repo in repos["repositories"]:
        desc: Json = repos["repositories"][repo]
        to_copy = desc.get("bootstrap", {}).get("copy")
        if to_copy:
            old_root: str = desc["repository"]["path"]
            new_root: str = os.path.join(copy_dir, repo)
            for x in to_copy:
                src: str = os.path.join(old_root, x)
                dst: str = os.path.normpath(os.path.join(new_root, x))

                if os.path.isdir(src):
                    shutil.copytree(src,
                                    dst,
                                    ignore=ignore_dst(dst),
                                    symlinks=False,
                                    dirs_exist_ok=True)
                elif os.path.isfile(src):
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    shutil.copyfile(src, dst, follow_symlinks=True)
                    shutil.copymode(src, dst, follow_symlinks=True)
            desc["repository"]["path"] = new_root
    os.unlink(repos_file)
    with open(repos_file, "w") as f:
        json.dump(repos, f, indent=2)


def bootstrap() -> None:
    if g_LOCAL_DEPS:
        print("Bootstrap build in %r from sources %r against LOCALBASE %r" %
              (g_WRKDIR, g_SRCDIR, g_LOCALBASE))
    else:
        print("Bootstrapping in %r from sources %r, taking files from %r" %
              (g_WRKDIR, g_SRCDIR, g_DISTDIR))
    os.makedirs(cast(str, g_WRKDIR), exist_ok=True)
    with open(os.path.join(cast(str, g_WRKDIR), "build-conf.json"), 'w') as f:
        json.dump(g_CONF, f, indent=2)
    src_wrkdir: str = os.path.normpath(os.path.join(cast(str, g_WRKDIR), "src"))
    shutil.copytree(g_SRCDIR, src_wrkdir, ignore=ignore_dst(src_wrkdir))
    if g_LOCAL_DEPS:
        config_to_local(repos_file=os.path.join(src_wrkdir, REPOS),
                        link_targets_file=os.path.join(src_wrkdir,
                                                       LOCAL_LINK_DIRS_MODULE,
                                                       "TARGETS"))
    empty_dir: str = os.path.join(cast(str, g_WRKDIR), "empty_directory")
    os.makedirs(empty_dir)
    prune_config(repos_file=os.path.join(src_wrkdir, REPOS),
                 empty_dir=empty_dir)
    copy_dir: str = os.path.join(cast(str, g_WRKDIR), "copied_roots")
    copy_roots(repos_file=os.path.join(src_wrkdir, REPOS), copy_dir=copy_dir)
    dep_flags = setup_deps(src_wrkdir)
    # handle proto
    flags = ["-I", src_wrkdir] + dep_flags["include"] + [
        "-I", os.path.join(g_LOCALBASE, "include")
    ]
    cpp_files: List[str] = []
    for root, dirs, files in os.walk(src_wrkdir):
        if 'test' in dirs:
            dirs.remove('test')
        if 'execution_api' in dirs:
            dirs.remove('execution_api')
        if 'serve_api' in dirs:
            dirs.remove('serve_api')
        if 'other_tools' in dirs:
            dirs.remove('other_tools')
        if 'archive' in dirs:
            dirs.remove('archive')
        for f in files:
            if f.endswith(".cpp"):
                cpp_files.append(os.path.join(root, f))
    object_files: List[str] = []
    with ThreadPoolExecutor(max_workers=1 if DEBUG else None) as ts:
        for f in cpp_files:
            obj_file_name = f[:-len(".cpp")] + ".o"
            object_files.append(obj_file_name)
            cmd: List[str] = BOOTSTRAP_CC + flags + [
                "-c", f, "-o", obj_file_name
            ]
            ts.submit(run, cmd, cwd=src_wrkdir)
    bootstrap_just: str = os.path.join(cast(str, g_WRKDIR), "bootstrap-just")
    final_cmd: List[str] = BOOTSTRAP_CC + g_FINAL_LDFLAGS + [
        "-o", bootstrap_just
    ] + object_files + dep_flags["link"]
    run(final_cmd, cwd=src_wrkdir)
    CONF_FILE: str = os.path.join(cast(str, g_WRKDIR), "repo-conf.json")
    LOCAL_ROOT: str = os.path.join(cast(str, g_WRKDIR), ".just")
    os.makedirs(LOCAL_ROOT, exist_ok=True)
    distdirs = " --distdir=".join(g_DISTDIR)
    run([
        "sh", "-c",
        "cp `./bin/just-mr.py --always-file -C %s --local-build-root=%s --distdir=%s setup just` %s"
        % (REPOS, LOCAL_ROOT, distdirs, CONF_FILE)
    ],
        cwd=src_wrkdir)
    GRAPH: str = os.path.join(cast(str, g_WRKDIR), "graph.json")
    TO_BUILD: str = os.path.join(cast(str, g_WRKDIR), "to_build.json")
    run([
        bootstrap_just, "analyse", "-C", CONF_FILE, "-D", CONF_STRING,
        "--dump-graph", GRAPH, "--dump-artifacts-to-build", TO_BUILD,
        MAIN_MODULE, MAIN_TARGET
    ],
        cwd=src_wrkdir)
    if DEBUG:
        traverser = "./bin/bootstrap-traverser.py"
    else:
        traverser = "./bin/parallel-bootstrap-traverser.py"
    run([
        traverser, "-C", CONF_FILE, "--default-workspace", src_wrkdir, GRAPH,
        TO_BUILD
    ],
        cwd=src_wrkdir)
    OUT: str = os.path.join(cast(str, g_WRKDIR), "out")
    run([
        "./out-boot/%s" %
        (MAIN_STAGE, ), "install", "-C", CONF_FILE, "-D", CONF_STRING, "-o",
        OUT, "--local-build-root", LOCAL_ROOT, MAIN_MODULE, MAIN_TARGET
    ],
        cwd=src_wrkdir)


def main(args: List[str]):
    global g_SRCDIR
    global g_WRKDIR
    global g_DISTDIR
    global g_LOCAL_DEPS
    global g_LOCALBASE
    global g_NON_LOCAL_DEPS
    if len(args) > 1:
        g_SRCDIR = os.path.abspath(args[1])
    if len(args) > 2:
        g_WRKDIR = os.path.abspath(args[2])
    if len(args) > 3:
        g_DISTDIR = [os.path.abspath(p) for p in args[3:]]

    if not g_WRKDIR:
        g_WRKDIR = tempfile.mkdtemp()
    if not g_DISTDIR:
        g_DISTDIR = [os.path.join(Path.home(), ".distfiles")]

    g_LOCAL_DEPS = "PACKAGE" in os.environ
    g_NON_LOCAL_DEPS = json.loads(os.environ.get("NON_LOCAL_DEPS", "[]"))
    bootstrap()


if __name__ == "__main__":
    # Parse options, set g_DISTDIR
    main(sys.argv)
