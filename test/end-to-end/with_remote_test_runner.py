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
import sys
import subprocess
import time

from typing import Any, Dict

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


def get_remote_execution_address(d: Json) -> str:
    return "%s:%d" % (d["interface"], int(d["port"]))


dump_results()

TEMP_DIR = os.path.realpath("scratch")
os.makedirs(TEMP_DIR, exist_ok=True)

WORK_DIR = os.path.realpath("work")
os.makedirs(WORK_DIR, exist_ok=True)

REMOTE_DIR = os.path.realpath("remote")
os.makedirs(REMOTE_DIR, exist_ok=True)
REMOTE_LBR = os.path.join(REMOTE_DIR, "build-root")
REMOTE_EXTRA_BINDIR = os.path.join(REMOTE_DIR, "bin")
os.makedirs(REMOTE_EXTRA_BINDIR, exist_ok=True)

g_REMOTE_EXECUTION_ADDRESS: str = ""

compatible = json.loads(sys.argv[1])

custom_remote = json.loads(sys.argv[2])

remote_proc = None
if not custom_remote:
    # start just execute as remote service
    REMOTE_INFO = os.path.join(REMOTE_DIR, "info.json")

    if os.path.exists(REMOTE_INFO):
        print(f"Warning: removing unexpected info file {REMOTE_INFO}")
        os.remove(REMOTE_INFO)

    PATH = subprocess.run(
        ["env", "--", "sh", "-c", "echo -n $PATH"],
        stdout=subprocess.PIPE,
    ).stdout.decode('utf-8')

    remote_cmd = [
        "./staged/bin/just",
        "execute",
        "-L",
        json.dumps(
            ["env",
             "PATH=" + REMOTE_EXTRA_BINDIR + (":" if PATH else "") + PATH]),
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

    remotestdout = open("remotestdout", "w")
    remotestderr = open("remotestderr", "w")
    remote_proc = subprocess.Popen(
        remote_cmd,
        stdout=remotestdout,
        stderr=remotestderr,
    )

    timeout: int = 30
    while not os.path.exists(REMOTE_INFO):
        if timeout == 0:
            result = "FAIL"
            stdout = "Failed to start execution service"
            remote_proc.terminate()
            dump_results()
            exit(1)
        timeout -= 1
        time.sleep(1)

    with open(REMOTE_INFO) as f:
        info = json.load(f)

    g_REMOTE_EXECUTION_ADDRESS = get_remote_execution_address(info)
else:
    msg = "\nA custom remote service is used, please look at logs there.\n"
    with open("remotestdout", "w") as f:
        print(msg, file=f)
    with open("remotestderr", "w") as f:
        print(msg, file=f)

    args = custom_remote.get("args", [])
    g_REMOTE_EXECUTION_ADDRESS = " ".join(
        (get_remote_execution_address(custom_remote), *args))

ENV = dict(os.environ,
           TEST_TMPDIR=TEMP_DIR,
           TMPDIR=TEMP_DIR,
           REMOTE_EXECUTION_ADDRESS=g_REMOTE_EXECUTION_ADDRESS,
           REMOTE_BIN=REMOTE_EXTRA_BINDIR)

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

if not custom_remote:
    assert remote_proc
    remote_proc.terminate()
    rout, rerr = remote_proc.communicate()

dump_results()

for f in sys.argv[2:]:
    keep_file = os.path.join(WORK_DIR, f)
    if not os.path.exists(keep_file):
        open(keep_file, "a").close()

if result != "PASS": exit(1)
