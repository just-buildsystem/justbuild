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

readonly ROOT="$(pwd)"
readonly LBR="${TMPDIR}/local-build-root"
readonly OUT="${TMPDIR}/out"
mkdir -p "${OUT}"
readonly JUST="${ROOT}/bin/tool-under-test"

mkdir work
cd work
touch ROOT

cat > RULES <<'EOF'
{ "complex action":
  { "expression":
    { "type": "RESULT"
    , "artifacts":
      { "type": "ACTION"
      , "cmd": ["sh", "-c", "mkdir -p foo && echo bar > foo/out.txt"]
      , "cwd": "deep/inside"
      , "outs": ["deep/inside/foo/out.txt"]
      , "execution properties":
        {"type": "'", "$1": {"image": "something-fancy", "runner": "special"}}
      , "timeout scaling": 2
      }
    }
  }
}
EOF

cat > TARGETS <<'EOF'
{"": {"type": "complex action"}}
EOF

#  Sanity check: build succeeds and gives the correct ouput
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}" \
          -L '["env", "PATH='"${PATH}"'"]' 2>&1
grep bar "${OUT}/deep/inside/foo/out.txt"

# Analyse first action and dump provides map
"${JUST}" analyse --local-build-root "${LBR}" \
          --request-action-input 0 \
          --dump-provides "${OUT}/provides.json" 2>&1

[ $(jq '."timeout scaling" | . == 2' "${OUT}/provides.json") = true ]
[ $(jq -r '.cwd' "${OUT}/provides.json") = "deep/inside" ]
[ $(jq  -r '."execution properties".image' "${OUT}/provides.json") = something-fancy ]
[ $(jq  -r '."execution properties".runner' "${OUT}/provides.json") = special ]

echo OK
