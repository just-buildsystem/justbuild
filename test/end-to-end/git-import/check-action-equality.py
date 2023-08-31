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
import sys

from typing import Any

Json = Any

def normalize(a: Json):
    for n in a["actions"].keys():
        del a["actions"][n]["origins"]

if __name__ == "__main__":
    with open(sys.argv[1]) as f:
        a: Json = json.load(f)
    with open(sys.argv[2]) as f:
        b: Json = json.load(f)
    normalize(a)
    normalize(b)
    if a != b:
        print("The action graphs in %s and %s differ!"
              % (sys.argv[1], sys.argv[2]))
        sys.exit(1)
