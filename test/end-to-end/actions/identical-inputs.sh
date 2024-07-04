#!/bin/sh
# Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

ROOT="$(pwd)"
JUST="${ROOT}/bin/tool-under-test"
LBR="${TEST_TMPDIR}/local-build-root"

mkdir sample-repo
cd sample-repo
touch ROOT

cat > TARGETS <<'EOF'
{ "many files":
  { "type": "generic"
  , "out_dirs": ["out"]
  , "cmds":
    [ "mkdir -p out"
    , "for i in $(seq 1 200000) ; do echo same-content > out/$i.txt ; done"
    ]
  }
, "":
  { "type": "generic"
  , "outs": ["all.txt"]
  , "deps": ["many files"]
  , "cmds": ["for f in out/*.txt ; do cat $f >> all.txt ; done"]
  }
}
EOF

"${JUST}" build --local-build-root "${LBR}" \
          -L '["env", "PATH='"${PATH}"'"]' 2>&1

echo OK
