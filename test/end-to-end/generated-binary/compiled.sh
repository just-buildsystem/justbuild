#!/bin/sh
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

set -e

mkdir .tool-root
touch ROOT
cat > TARGETS <<'EOI'
{ "program outputs": {"type": "generated files", "width": ["16"]}
, "ALL":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["cat $(ls out-*.txt | sort) > out.txt"]
  , "deps": ["program outputs"]
  }
}
EOI

echo
echo "Analysing"
bin/tool-under-test analyse --dump-graph graph.json 2>&1

echo
echo "Building"
bin/tool-under-test install -o out --local-build-root .tool-root -J 16 2>&1
