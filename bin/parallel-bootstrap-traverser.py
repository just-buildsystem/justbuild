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
import multiprocessing
import os
import shutil
import subprocess
import sys
import threading
from enum import Enum
from argparse import ArgumentParser
from typing import Any, Callable, Dict, List, Optional, Tuple, cast

# generic JSON type that avoids getter issues; proper use is being enforced by
# return types of methods and typing vars holding return values of json getters
Json = Dict[str, Any]


class AtomicInt:
    # types of attributes
    __value: int
    __cv: threading.Condition

    def __init__(self, init: int = 0) -> None:
        self.__value = init
        self.__cv = threading.Condition()

    def __notify(self) -> None:
        """Must be called with acquired cv."""
        if self.__value == 0: self.__cv.notify_all()

    @property
    def value(self) -> int:
        with self.__cv:
            return self.__value

    @value.setter
    def value(self, to: int) -> None:
        with self.__cv:
            self.__value = to
            self.__notify()

    def fetch_inc(self, by: int = 1) -> int:
        """Post-increment"""
        with self.__cv:
            val = self.__value
            self.__value += by
            self.__notify()
        return val

    def fetch_dec(self, by: int = 1) -> int:
        """Post-decrement"""
        return self.fetch_inc(-by)

    def wait_for_zero(self) -> None:
        with self.__cv:
            self.__cv.wait_for(lambda: self.__value == 0)


class TaskSystem:
    """Simple queue-based task system."""
    Task = Tuple[Callable[..., None], Tuple[Any, ...], Dict[str, Any]]

    class Queue:
        cv: threading.Condition

        def __init__(self) -> None:
            self.cv = threading.Condition()
            self.tasks: List[TaskSystem.Task] = []

    # types of attributes
    __shutdown: bool
    __num_workers: int
    __current_idx: AtomicInt
    __qs: List[Queue]
    __total_work: AtomicInt
    __workers: List[threading.Thread]

    def __init__(self, max_workers: int = multiprocessing.cpu_count()) -> None:
        """Creates the task system with `max_workers` many threads."""
        self.__shutdown = False
        self.__num_workers = max(1, max_workers)
        self.__current_idx = AtomicInt()
        self.__qs = [TaskSystem.Queue() for _ in range(self.__num_workers)]

        def run(q: TaskSystem.Queue, idx: int):
            try:
                while not self.__shutdown:
                    task: Optional[TaskSystem.Task] = None
                    with q.cv:
                        if len(q.tasks) == 0:
                            self.__total_work.fetch_dec()  # suspend thread
                            q.cv.wait_for(
                                lambda: len(q.tasks) > 0 or self.__shutdown)
                            self.__total_work.fetch_inc()  # active thread
                        if len(q.tasks) > 0 and not self.__shutdown:
                            task = q.tasks.pop(0)
                            self.__total_work.fetch_dec()
                    if task:
                        task[0](*task[1], **task[2])
            except Exception as e:
                self.__shutdown = True
                raise e
            finally:
                self.__total_work.value = 0

        # total work = num(queued tasks) + num(active threads)
        self.__total_work = AtomicInt(self.__num_workers)  # initially active
        self.__workers = [
            threading.Thread(target=run, args=(self.__qs[i], i))
            for i in range(self.__num_workers)
        ]
        for w in self.__workers:
            w.start()

    def __enter__(self):
        return self

    def __exit__(self, exc_type: Any, exc_value: Any,
                 exc_traceback: Any) -> None:
        try:
            self.finish()
        finally:
            self.shutdown()

    def add(self, fn: Callable[..., None], *args: Any, **kw: Any) -> None:
        """Add task to task queue, might block."""
        if not self.__shutdown:
            q = self.__qs[self.__current_idx.fetch_inc() % self.__num_workers]
            with q.cv:
                self.__total_work.fetch_inc()
                q.tasks.append((fn, args, kw))
                q.cv.notify_all()

    def finish(self) -> None:
        """Wait for queued tasks and active threads to become zero."""
        self.__total_work.wait_for_zero()

    def shutdown(self) -> None:
        """Initiate shutdown of task system and wait for all threads to stop."""
        self.__shutdown = True  # signal shutdown
        for q in self.__qs:  # notify everyone about shutdown
            with q.cv:
                q.cv.notify_all()
        for w in self.__workers:  # wait for workers to shutdown
            w.join()


class AtomicListMap:
    class Entry(Enum):
        CREATED = 0
        INSERTED = 1
        CLEARED = 2

    # types of attributes
    __map: Dict[str, Optional[List[Any]]]
    __lock: threading.Lock

    def __init__(self) -> None:
        self.__map = dict()
        self.__lock = threading.Lock()

    def add(self, key: str, val: Any):
        with self.__lock:
            if key not in self.__map:
                self.__map[key] = [val]
                return AtomicListMap.Entry.CREATED
            elif self.__map[key] != None:
                cast(List[Any], self.__map[key]).append(val)
                return AtomicListMap.Entry.INSERTED
            else:
                return AtomicListMap.Entry.CLEARED

    def fetch_clear(self, key: str) -> Optional[List[Any]]:
        with self.__lock:
            vals = self.__map[key]
            self.__map[key] = None
            return vals


g_CALLBACKS_PER_ID = AtomicListMap()


def log(*args: str, **kwargs: Any):
    print(*args, file=sys.stderr, **kwargs)


def fail(s: str):
    log(s)
    sys.exit(1)


def git_hash(content: bytes) -> str:
    header = "blob {}\0".format(len(content)).encode('utf-8')
    h = hashlib.sha1()
    h.update(header)
    h.update(content)
    return h.hexdigest()


def create_blobs(blobs: List[str], *, root: str, ts: TaskSystem) -> None:
    os.makedirs(os.path.join(root, "KNOWN"))

    def write_blob(blob_bin: bytes) -> None:
        with open(os.path.join(root, "KNOWN", git_hash(blob_bin)), "wb") as f:
            f.write(blob_bin)

    for blob in blobs:
        ts.add(write_blob, blob.encode('utf-8'))


def build_known(desc: Json, *, root: str) -> str:
    return os.path.join(root, "KNOWN", desc["data"]["id"])


def link(src: str, dest: str) -> None:
    dest = os.path.normpath(dest)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    try:
        os.link(src, dest)
    except:
        os.symlink(src, dest)


def build_local(desc: Json, *, root: str, config: Json) -> Optional[str]:
    repo_name: str = desc["data"]["repository"]
    repo: List[str] = config["repositories"][repo_name]["workspace_root"]
    rel_path: str = desc["data"]["path"]
    if repo[0] == "file":
        return os.path.join(repo[1], rel_path)
    fail("Unsupported repository root %r" % (repo, ))


def build_tree(desc: Json, *, config: Json, root: str, graph: Json,
               ts: TaskSystem, callback: Callable[..., None]):
    tree_id = desc["data"]["id"]
    tree_dir = os.path.normpath(os.path.join(root, "TREE", tree_id))
    state: AtomicListMap.Entry = g_CALLBACKS_PER_ID.add(f"TREE/{tree_id}",
                                                        callback)
    if state != AtomicListMap.Entry.CREATED:  # we are not first
        if state != AtomicListMap.Entry.INSERTED:  # tree ready, run callback
            callback(tree_dir)
        return
    tree_dir_tmp = tree_dir + ".tmp"
    tree_desc: Json = graph["trees"][tree_id]

    num_entries = AtomicInt(len(tree_desc.items()))

    def run_callbacks() -> None:
        if num_entries.fetch_dec() <= 1:
            # correctly handle the empty tree
            os.makedirs(tree_dir_tmp, exist_ok=True)
            shutil.copytree(tree_dir_tmp, tree_dir)
            vals = g_CALLBACKS_PER_ID.fetch_clear(f"TREE/{tree_id}")
            if vals:
                for cb in vals:  # mark ready
                    ts.add(cb, tree_dir)

    if num_entries.value == 0:
        run_callbacks()

    for location, desc in tree_desc.items():

        def create_link(location: str) -> Callable[..., None]:
            def do_link(path: str) -> None:
                link(path, os.path.join(tree_dir_tmp, location))
                run_callbacks()

            return do_link

        ts.add(build,
               desc,
               config=config,
               root=root,
               graph=graph,
               ts=ts,
               callback=create_link(location))


def run_action(action_id: str, *, config: Json, root: str, graph: Json,
               ts: TaskSystem, callback: Callable[..., None]) -> None:
    action_dir: str = os.path.normpath(os.path.join(root, "ACTION", action_id))
    state: AtomicListMap.Entry = g_CALLBACKS_PER_ID.add(f"ACTION/{action_id}",
                                                        callback)
    if state != AtomicListMap.Entry.CREATED:  # we are not first
        if state != AtomicListMap.Entry.INSERTED:  # action ready, run callback
            callback(action_dir)
        return
    os.makedirs(action_dir)
    action_desc: Json = graph["actions"][action_id]

    num_deps = AtomicInt(len(action_desc.get("input", {}).items()))

    def run_command_and_callbacks() -> None:
        if num_deps.fetch_dec() <= 1:
            cmd: List[str] = action_desc["command"]
            env = action_desc.get("env")
            log("Running %r with env %r for action %r" % (cmd, env, action_id))
            for out in action_desc["output"]:
                os.makedirs(os.path.join(action_dir, os.path.dirname(out)),
                            exist_ok=True)
            subprocess.run(cmd, env=env, cwd=action_dir, check=True)
            vals = g_CALLBACKS_PER_ID.fetch_clear(f"ACTION/{action_id}")
            if vals:
                for cb in vals:  # mark ready
                    ts.add(cb, action_dir)

    if num_deps.value == 0:
        run_command_and_callbacks()

    for location, desc in action_desc.get("input", {}).items():

        def create_link(location: str) -> Callable[..., None]:
            def do_link(path: str) -> None:
                link(path, os.path.join(action_dir, location))
                run_command_and_callbacks()

            return do_link

        ts.add(build,
               desc,
               config=config,
               root=root,
               graph=graph,
               ts=ts,
               callback=create_link(location))


def build_action(desc: Json, *, config: Json, root: str, graph: Json,
                 ts: TaskSystem, callback: Callable[..., None]) -> None:
    def link_output(action_dir: str) -> None:
        callback(os.path.join(action_dir, desc["data"]["path"]))

    run_action(desc["data"]["id"],
               config=config,
               root=root,
               graph=graph,
               ts=ts,
               callback=link_output)


def build(desc: Json, *, config: Json, root: str, graph: Json, ts: TaskSystem,
          callback: Callable[..., None]) -> None:
    if desc["type"] == "TREE":
        build_tree(desc,
                   config=config,
                   root=root,
                   graph=graph,
                   ts=ts,
                   callback=callback)
    elif desc["type"] == "ACTION":
        build_action(desc,
                     config=config,
                     root=root,
                     graph=graph,
                     ts=ts,
                     callback=callback)
    elif desc["type"] == "KNOWN":
        callback(build_known(desc, root=root))
    elif desc["type"] == "LOCAL":
        callback(build_local(desc, root=root, config=config))
    else:
        fail("Don't know how to build artifact %r" % (desc, ))


def traverse(*, graph: Json, to_build: Json, out: str, root: str,
             config: Json) -> None:
    os.makedirs(out, exist_ok=True)
    os.makedirs(root, exist_ok=True)
    with TaskSystem() as ts:
        create_blobs(graph["blobs"], root=root, ts=ts)
        ts.finish()
        for location, artifact in to_build.items():

            def create_link(location: str) -> Callable[..., None]:
                return lambda path: link(path, os.path.join(out, location))

            ts.add(build,
                   artifact,
                   config=config,
                   root=root,
                   graph=graph,
                   ts=ts,
                   callback=create_link(location))


def main():
    parser = ArgumentParser()
    parser.add_argument("-C",
                        dest="repository_config",
                        help="Repository-description file to use",
                        metavar="FILE")
    parser.add_argument("-o",
                        dest="output_directory",
                        help="Directory to place output to")
    parser.add_argument("--local-build-root",
                        dest="local_build_root",
                        help="Root for storing intermediate outputs",
                        metavar="PATH")
    parser.add_argument("--default-workspace",
                        dest="default_workspace",
                        help="Workspace root to use if none is specified",
                        metavar="PATH")

    (options, args) = parser.parse_known_args()
    if len(args) != 2:
        fail("usage: %r <graph> <targets_to_build>" % (sys.argv[0], ))
    with open(args[0]) as f:
        graph: Json = json.load(f)
    with open(args[1]) as f:
        to_build: Json = json.load(f)
    out: str = os.path.abspath(cast(str, options.output_directory
                                    or "out-boot"))
    root: str = os.path.abspath(
        cast(str, options.local_build_root or ".just-boot"))
    with open(options.repository_config or "repo-conf.json") as f:
        config = json.load(f)
    if options.default_workspace:
        ws_root: str = os.path.abspath(options.default_workspace)
        repos: List[str] = config.get("repositories", {}).keys()
        for repo in repos:
            if not "workspace_root" in config["repositories"][repo]:
                config["repositories"][repo]["workspace_root"] = [
                    "file", ws_root
                ]
    traverse(graph=graph, to_build=to_build, out=out, root=root, config=config)


if __name__ == "__main__":
    main()
