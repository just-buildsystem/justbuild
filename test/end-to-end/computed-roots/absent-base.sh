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

set -eu

env

readonly ROOT="${PWD}"
readonly JUST="${ROOT}/bin/tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly MAIN="${ROOT}/main"
readonly OUT="${TEST_TMPDIR}/out"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi


mkdir -p "${MAIN}"
cd "${MAIN}"

cat > repo-config.json <<EOF
{ "repositories":
  { "base": {"workspace_root": ["git tree", "${TREE_0}"]}
  , "":
    { "workspace_root": ["computed", "base", "", "", {"COUNT": "10"}]
    , "target_root": ["file", "${MAIN}/targets"]
    }
  }
}
EOF
cat repo-config.json

mkdir -p targets
cat > targets/TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["out"]
  , "cmds": ["cat *.txt > out"]
  , "deps": [["data", ""]]
  }
}
EOF
mkdir -p targets/data
cat > targets/data/TARGETS <<'EOF'
{"": {"type": "install", "deps": [["GLOB", null, "*.txt"]]}}
EOF

echo
echo Build against computed root with absent base
echo
"${JUST}" install -o "${OUT}" -C repo-config.json \
          --local-build-root "${LBR}" \
          --remote-serve-address ${SERVE} \
          -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
          --log-limit 4 2>&1
# sanity check output
[ $(cat "${OUT}/out" | wc -l) -eq 55 ]

echo OK
