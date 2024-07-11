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
import os
import shutil
import subprocess
import sys
import time

from typing import Any, Dict, List

Json = Dict[str, Any]

time_start: float = time.time()
time_stop: float = 0
result: str = "UNKNOWN"
stderr: str = ""
stdout: str = ""


def dump_results() -> None:
    with open("result", "w") as f:
        f.write("%s\n" % (result, ))
    with open("time-start", "w") as f:
        f.write("%d\n" % (time_start, ))
    with open("time-stop", "w") as f:
        f.write("%d\n" % (time_stop, ))
    with open("stdout", "w") as f:
        f.write("%s\n" % (stdout, ))
    with open("stderr", "w") as f:
        f.write("%s\n" % (stderr, ))
    with open("pwd", "w") as f:
        f.write("%s\n" % (os.getcwd(), ))


def run_cmd(cmd: List[str],
            *,
            env: Any = None,
            stdout: Any = subprocess.DEVNULL,
            stderr: Any = None,
            cwd: str):
    subprocess.run(cmd, cwd=cwd, env=env, stdout=stdout, stderr=stderr)


def get_remote_execution_address(d: Json) -> str:
    return "%s:%d" % (d["interface"], int(d["port"]))


dump_results()

TEMP_DIR = os.path.abspath(os.path.realpath("scratch"))
os.makedirs(TEMP_DIR, exist_ok=True)

WORK_DIR = os.path.abspath(os.path.realpath("work"))
os.makedirs(WORK_DIR, exist_ok=True)

REMOTE_DIR = os.path.abspath(os.path.realpath("remote"))
os.makedirs(REMOTE_DIR, exist_ok=True)
REMOTE_LBR = os.path.join(REMOTE_DIR, "build-root")

g_REMOTE_EXECUTION_ADDRESS: str = ""

SERVE_DIR = os.path.abspath(os.path.realpath("serve"))
os.makedirs(SERVE_DIR, exist_ok=True)
SERVE_LBR = os.path.join(SERVE_DIR, "build-root")

compatible = json.loads(sys.argv[1])
standalone_serve = json.loads(sys.argv[2])

remote_proc = None

PATH = subprocess.run(
    ["env", "--", "sh", "-c", "echo -n $PATH"],
    stdout=subprocess.PIPE,
).stdout.decode('utf-8')

remotestdout = open("remotestdout", "w")
remotestderr = open("remotestderr", "w")
if not standalone_serve:
    # start just execute as remote service
    REMOTE_INFO = os.path.join(REMOTE_DIR, "remote-info.json")

    if os.path.exists(REMOTE_INFO):
        print(f"Warning: removing unexpected info file {REMOTE_INFO}")
        os.remove(REMOTE_INFO)

    remote_cmd = [
        "./staged/bin/just",
        "execute",
        "-L",
        json.dumps(["env", "PATH=" + PATH]),
        "--info-file",
        REMOTE_INFO,
        "--local-build-root",
        REMOTE_LBR,
        "--log-limit",
        "6",
        "--plain-log",
    ]

    if compatible:
        remote_cmd.append("--compatible")

    remote_proc = subprocess.Popen(
        remote_cmd,
        stdout=remotestdout,
        stderr=remotestderr,
    )

    while not os.path.exists(REMOTE_INFO):
        time.sleep(1)

    with open(REMOTE_INFO) as f:
        info = json.load(f)

    g_REMOTE_EXECUTION_ADDRESS = get_remote_execution_address(info)

# start just serve service
SERVE_INFO = os.path.join(SERVE_DIR, "serve-info.json")
SERVE_CONFIG_FILE = os.path.join(SERVE_DIR, "serve.json")

serve_config: Json = {}

if standalone_serve:
    serve_config = {
        "local build root": {
            "root": "system",
            "path": SERVE_LBR
        },
        "logging": {
            "limit": 6,
            "plain": True
        },
        "execution endpoint": {
            "compatible": compatible
        },
        "remote service": {
            "info file": {
                "root": "system",
                "path": SERVE_INFO
            }
        },
        "build": {
            "local launcher": ["env", "PATH=" + PATH]
        },
    }
else:
    serve_config = {
        "local build root": {
            "root": "system",
            "path": SERVE_LBR
        },
        "logging": {
            "limit": 6,
            "plain": True
        },
        "execution endpoint": {
            "address": g_REMOTE_EXECUTION_ADDRESS,
            "compatible": compatible
        },
        "remote service": {
            "info file": {
                "root": "system",
                "path": SERVE_INFO
            }
        },
    }

repositories: List[Dict[str, str]] = []  # list of location objects
repos_env: Dict[str, str] = {}

REPOS_DIR = os.path.realpath("repos")
os.makedirs(REPOS_DIR, exist_ok=True)
DATA_DIR = os.path.realpath("data")
os.makedirs(DATA_DIR, exist_ok=True)

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

count = 0
repo_data = sorted(os.listdir("data"))
for repo in repo_data:
    target = os.path.join(REPOS_DIR, repo)
    shutil.copytree(
        os.path.join(DATA_DIR, repo),
        target,
    )
    run_cmd(
        ["git", "init"],
        cwd=target,
        env=dict(os.environ, **GIT_NOBODY_ENV),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    run_cmd(
        ["git", "add", "-f", "."],
        cwd=target,
        env=dict(os.environ, **GIT_NOBODY_ENV),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    run_cmd(
        ["git", "commit", "-m",
         "Content of %s" % (target, )],
        cwd=target,
        env=dict(os.environ, **GIT_NOBODY_ENV),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    repositories.append({"root": "system", "path": target})
    repos_env["COMMIT_%d" % count] = subprocess.run(
        ["git", "log", "-n", "1", "--format=%H"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        cwd=target).stdout.decode('utf-8').strip()
    repos_env["TREE_%d" % count] = subprocess.run(
        ["git", "log", "-n", "1", "--format=%T"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        cwd=target).stdout.decode('utf-8').strip()
    # increase counter
    count += 1

serve_config["repositories"] = repositories

with open(SERVE_CONFIG_FILE, "w") as f:
    json.dump(serve_config, f)

servestdout = open("servestdout", "w")
servestderr = open("servestderr", "w")
serve_proc = subprocess.Popen(
    ["./staged/bin/just", "serve", SERVE_CONFIG_FILE],
    stdout=servestdout,
    stderr=servestderr,
)

timeout: int = 30
while not os.path.exists(SERVE_INFO):
    if timeout == 0:
        result = "FAIL"
        stdout = "Failed to start serve service"
        serve_proc.terminate()
        dump_results()
        exit(1)
    timeout -= 1
    time.sleep(1)

with open(SERVE_INFO) as f:
    serve_info = json.load(f)

SERVE_ADDRESS = get_remote_execution_address(serve_info)

# run the actual test

ENV = dict(
    os.environ,
    TEST_TMPDIR=TEMP_DIR,
    TMPDIR=TEMP_DIR,
    REMOTE_EXECUTION_ADDRESS=(g_REMOTE_EXECUTION_ADDRESS
                              if not standalone_serve else SERVE_ADDRESS),
    REMOTE_LBR=(REMOTE_LBR if not standalone_serve else SERVE_LBR),
    SERVE=SERVE_ADDRESS,
    SERVE_LBR=SERVE_LBR,  # expose the serve build root to the test env
    **repos_env)

if standalone_serve:
    ENV["STANDALONE_SERVE"] = "YES"
elif "STANDALONE_SERVE" in ENV:
    del ENV["STANDALONE_SERVE"]
if compatible:
    ENV["COMPATIBLE"] = "YES"
elif "COMPATIBLE" in ENV:
    del ENV["COMPATIBLE"]

for k in ["TLS_CA_CERT", "TLS_CLIENT_CERT", "TLS_CLIENT_KEY"]:
    if k in ENV:
        del ENV[k]

time_start = time.time()
ret = subprocess.run(["sh", "../test.sh"],
                     cwd=WORK_DIR,
                     env=ENV,
                     capture_output=True)

time_stop = time.time()
result = "PASS" if ret.returncode == 0 else "FAIL"
stdout = ret.stdout.decode("utf-8")
stderr = ret.stderr.decode("utf-8")

if not standalone_serve:
    assert remote_proc
    remote_proc.terminate()
    rout, rerr = remote_proc.communicate()

assert serve_proc
serve_proc.terminate()
sout, serr = serve_proc.communicate()

dump_results()

for f in sys.argv[3:]:
    keep_file = os.path.join(WORK_DIR, f)
    if not os.path.exists(keep_file):
        open(keep_file, "a").close()

if result != "PASS": exit(1)
