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

import json
import os
import subprocess
import time

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


dump_results()

TEMP_DIR = os.path.realpath("scratch")
os.makedirs(TEMP_DIR, exist_ok=True)

WORK_DIR = os.path.realpath("work")
os.makedirs(WORK_DIR, exist_ok=True)

REMOTE_DIR = os.path.realpath("remote")
os.makedirs(REMOTE_DIR, exist_ok=True)

REMOTE_INFO = os.path.join(REMOTE_DIR, "info.json")
REMOTE_LBR = os.path.join(REMOTE_DIR, "build-root")

if os.path.exists(REMOTE_INFO):
    print(f"Warning: removing unexpected info file {REMOTE_INFO}")
    os.remove(REMOTE_INFO)

PATH=subprocess.run(
    ["env", "--", "sh", "-c", "echo -n $PATH"],
    stdout=subprocess.PIPE,
).stdout.decode('utf-8')

remote_cmd = [
    "./bin/just",
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

with open("compatible-remote.json") as f:
    compatible = json.load(f)

if compatible:
    remote_cmd.append("--compatible")

remotestdout = open("remotestdout", "w")
remotestderr = open("remotestderr", "w")
remote_proc = subprocess.Popen(
    remote_cmd,
    stdout=remotestdout,
    stderr=remotestderr,
)

while not os.path.exists(REMOTE_INFO):
    time.sleep(1)

with open(REMOTE_INFO) as f:
    info = json.load(f)

REMOTE_EXECUTION_ADDRESS = "%s:%d" % (info["interface"], info["port"])

ENV = dict(os.environ,
           TEST_TMPDIR=TEMP_DIR,
           TMPDIR=TEMP_DIR,
           REMOTE_EXECUTION_ADDRESS=REMOTE_EXECUTION_ADDRESS)

if compatible:
    ENV["COMPATIBLE"] = "YES"
elif "COMPATIBLE" in ENV:
    del ENV["COMPATIBLE"]

for k in ["TLS_CA_CERT", "TLS_CLIENT_CERT", "TLS_CLIENT_KEY"]:
    if k in ENV:
        del ENV[k]

with open('test-launcher.json') as f:
    test_launcher = json.load(f)

with open('test-args.json') as f:
    test_args = json.load(f)

time_start = time.time()

ret = subprocess.run(test_launcher + ["../test"] + test_args,
                     cwd=WORK_DIR,
                     env=ENV,
                     capture_output=True)

time_stop = time.time()
result = "PASS" if ret.returncode == 0 else "FAIL"
stdout = ret.stdout.decode("utf-8")
stderr = ret.stderr.decode("utf-8")
remote_proc.terminate()
rout, rerr = remote_proc.communicate()

dump_results()

if result != "PASS": exit(1)
