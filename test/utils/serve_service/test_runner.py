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

SERVE_CONFIG_FILE = os.path.realpath("just-servec")

# Set known serve repository roots under TEST_TMPDIR
TEST_SERVE_REPO_1 = os.path.join(TEMP_DIR, "test_serve_repo_1")
if os.path.exists(TEST_SERVE_REPO_1):
    os.remove(TEST_SERVE_REPO_1)
TEST_SERVE_REPO_2 = os.path.join(TEMP_DIR, "test_serve_repo_2")
if os.path.exists(TEST_SERVE_REPO_2):
    os.remove(TEST_SERVE_REPO_2)

SERVE_REPOSITORIES = ";".join([TEST_SERVE_REPO_1, TEST_SERVE_REPO_2])

REMOTE_SERVE_INFO = os.path.join(REMOTE_DIR, "info_serve.json")
SERVE_LBR = os.path.join(REMOTE_DIR, "serve-build-root")

if os.path.exists(REMOTE_SERVE_INFO):
    print(f"Warning: removing unexpected info file {REMOTE_SERVE_INFO}")
    os.remove(REMOTE_SERVE_INFO)

# Run 'just serve'

with open(SERVE_CONFIG_FILE, "w") as f:
    f.write(
        json.dumps({
            "repositories": [TEST_SERVE_REPO_1, TEST_SERVE_REPO_2],
            "logging": {
                "limit": 6,
                "plain": True
            },
            "remote service": {
                "info file": REMOTE_SERVE_INFO
            },
            "local build root": SERVE_LBR
        }))

serve_cmd = ["./bin/just", "serve", SERVE_CONFIG_FILE]

servestdout = open("servestdout", "w")
servestderr = open("servestderr", "w")
serve_proc = subprocess.Popen(
    serve_cmd,
    stdout=servestdout,
    stderr=servestderr,
)

while not os.path.exists(REMOTE_SERVE_INFO):
    time.sleep(1)

with open(REMOTE_SERVE_INFO) as f:
    serve_info = json.load(f)

REMOTE_SERVE_ADDRESS = "%s:%d" % (serve_info["interface"], serve_info["port"])

# Setup environment

ENV = dict(os.environ,
           TEST_TMPDIR=TEMP_DIR,
           TMPDIR=TEMP_DIR,
           REMOTE_SERVE_ADDRESS=REMOTE_SERVE_ADDRESS,
           SERVE_REPOSITORIES=SERVE_REPOSITORIES)

for k in ["TLS_CA_CERT", "TLS_CLIENT_CERT", "TLS_CLIENT_KEY"]:
    if k in ENV:
        del ENV[k]

with open('test-launcher.json') as f:
    test_launcher = json.load(f)

with open('test-args.json') as f:
    test_args = json.load(f)

# Run the test

time_start = time.time()

ret = subprocess.run(test_launcher + ["../test"] + test_args,
                     cwd=WORK_DIR,
                     env=ENV,
                     capture_output=True)

time_stop = time.time()
result = "PASS" if ret.returncode == 0 else "FAIL"
stdout = ret.stdout.decode("utf-8")
stderr = ret.stderr.decode("utf-8")
serve_proc.terminate()
sout, serr = serve_proc.communicate()

dump_results()

if result != "PASS": exit(1)
