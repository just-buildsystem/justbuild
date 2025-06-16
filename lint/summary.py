#!/usr/bin/env python3
# Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

from typing import Any, Dict

FAILED: Dict[str, Any] = {}

for lint in sorted(os.listdir()):
    if os.path.isdir(lint):
        with open(os.path.join(lint, "result")) as f:
            result = f.read().strip()
        if result != "PASS":
            record = {}
            with open(os.path.join(lint, "out/config.json")) as f:
                record["config"] = json.load(f)
            with open(os.path.join(lint, "stdout")) as f:
                log = f.read()
            with open(os.path.join(lint, "stderr")) as f:
                log += f.read()
            record["log"] = log
            FAILED[lint] = record

OUT = os.environ.get("OUT")
if OUT is None:
    print("Failed to get OUT")
    sys.exit(1)

with open(os.path.join(OUT, "failures.json"), "w") as f:
    json.dump(FAILED, f)

failures = list(FAILED.keys())

for f in failures:
    src = FAILED[f]["config"]["src"]
    log = FAILED[f]["log"]

    print("%s %s" % (f, src))
    print("".join(["    " + line + "\n" for line in log.splitlines()]))

if failures:
    sys.exit(1)
