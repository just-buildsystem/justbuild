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

import sys
import ast
import json
import yaml  # this package has lax type hints

from typing import Any

if len(sys.argv) < 4:
    print(f"usage: {sys.argv[0]} [py|json|yaml] [py|json|yaml] <file>")
    sys.exit(1)

with open(sys.argv[3]) as f:
    data: Any = {}
    if sys.argv[1] == "py":
        data = ast.literal_eval(f.read())
    elif sys.argv[1] == "json":
        data = json.load(f)
    elif sys.argv[1] == "yaml":
        data = yaml.load(f, Loader=yaml.Loader)  # type: ignore

    if (sys.argv[2] == "py"):
        print(data)
    elif sys.argv[2] == "json":
        print(json.dumps(data, indent=2))
    elif sys.argv[2] == "yaml":
        print(yaml.dump(data, indent=2))  # type: ignore
