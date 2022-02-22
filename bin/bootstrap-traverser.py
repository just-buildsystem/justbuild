#!/usr/bin/env python3

import hashlib
import json
import os
import shutil
import subprocess
import sys

from optparse import OptionParser

def log(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)

def fail(s):
    log(s)
    sys.exit(1)

def git_hash(content):
  header = "blob {}\0".format(len(content)).encode('utf-8')
  h = hashlib.sha1()
  h.update(header)
  h.update(content)
  return h.hexdigest()

def create_blobs(blobs, *, root):
    os.makedirs(os.path.join(root, "KNOWN"))
    for blob in blobs:
        blob_bin = blob.encode('utf-8')
        with open(os.path.join(root, "KNOWN", git_hash(blob_bin)), "wb") as f:
            f.write(blob_bin)

def build_known(desc, *, root):
    return os.path.join(root, "KNOWN", desc["data"]["id"])

def link(src, dest):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    os.symlink(src, dest)

def build_local(desc, *, root, config):
    repo_name = desc["data"]["repository"]
    repo = config["repositories"][repo_name]["workspace_root"]
    rel_path = desc["data"]["path"]
    if repo[0] == "file":
        return os.path.join(repo[1], rel_path)
    fail("Unsupported repository root %r" % (repo,))

def build_tree(desc, *, config, root, graph):
    tree_id = desc["data"]["id"]
    tree_dir = os.path.normpath(os.path.join(root, "TREE", tree_id))
    if os.path.isdir(tree_dir):
        return tree_dir
    os.makedirs(tree_dir)
    tree_desc = graph["trees"][tree_id]
    for location, desc in tree_desc.items():
        link(build(desc, config=config, root=root, graph=graph),
             os.path.join(tree_dir, location))
    return tree_dir

def run_action(action_id, *, config, root, graph):
    action_dir = os.path.normpath(os.path.join(root, "ACTION", action_id))
    if os.path.isdir(action_dir):
        return action_dir
    os.makedirs(action_dir)
    action_desc = graph["actions"][action_id]
    for location, desc in action_desc["input"].items():
        link(build(desc, config=config, root=root, graph=graph),
             os.path.join(action_dir, location))
    cmd = action_desc["command"]
    env = action_desc.get("env")
    log("Running %r with env %r for action %r"
        % (cmd, env, action_id))
    for out in action_desc["output"]:
        os.makedirs(os.path.join(action_dir, os.path.dirname(out)),
                    exist_ok=True)
    subprocess.run(cmd, env=env, cwd=action_dir, check=True)
    return action_dir

def build_action(desc, *, config, root, graph):
    action_dir = run_action(desc["data"]["id"], config=config, root=root, graph=graph)
    return os.path.join(action_dir, desc["data"]["path"])

def build(desc, *, config, root, graph):
    if desc["type"] == "TREE":
        return build_tree(desc, config=config, root=root, graph=graph)
    if desc["type"] == "ACTION":
        return build_action(desc, config=config, root=root, graph=graph)
    if desc["type"] == "KNOWN":
        return build_known(desc, root=root)
    if desc["type"] == "LOCAL":
        return build_local(desc, root=root, config=config)
    fail("Don't know how to build artifact %r" % (desc,))

def traverse(*, graph, to_build, out, root, config):
    os.makedirs(out, exist_ok=True)
    os.makedirs(root, exist_ok=True)
    create_blobs(graph["blobs"], root=root)
    for location, artifact in to_build.items():
        link(build(artifact, config=config, root=root, graph=graph),
             os.path.join(out, location))

def main():
    parser = OptionParser()
    parser.add_option("-C", dest="repository_config",
                      help="Repository-description file to use",
                      metavar="FILE")
    parser.add_option("-o", dest="output_directory",
                      help="Directory to place output to")
    parser.add_option("--local_build_root", dest="local_build_root",
                      help="Root for storing intermediate outputs",
                      metavar="PATH")
    parser.add_option("--default_workspace", dest="default_workspace",
                      help="Workspace root to use if none is specified",
                      metavar="PATH")
    (options, args) = parser.parse_args()
    if len(args) != 2:
        fail("usage: %r <graph> <targets_to_build>"
             % (sys.argv[0],))
    with open(args[0]) as f:
        graph = json.load(f)
    with open(args[1]) as f:
        to_build = json.load(f)
    out = os.path.abspath(options.output_directory or "out-boot")
    root = os.path.abspath(options.local_build_root or ".just-boot")
    with open(options.repository_config or "repo-conf.json") as f:
        config = json.load(f)
    if options.default_workspace:
        ws_root = os.path.abspath(options.default_workspace)
        repos = config.get("repositories", {}).keys()
        for repo in repos:
            if not "workspace_root" in config["repositories"][repo]:
                config["repositories"][repo]["workspace_root"] = ["file", ws_root]
    traverse(graph=graph, to_build=to_build, out=out, root=root, config=config)


if __name__ == "__main__":
    main()
