#!/bin/sh
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

set -eu

env

readonly ROOT="${PWD}"
readonly JUST="${ROOT}/bin/tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly MAIN="${ROOT}/main"
readonly TARGETS="${ROOT}/targets"
readonly OUT="${TEST_TMPDIR}/out"
readonly LOG="${TEST_TMPDIR}/overall.log"
readonly BUILD_LOG="${OUT}/build.log"

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
    { "workspace_root":
      ["computed", "base", "", "", {"COUNT": "10"}, {"absent": true}]
    , "target_root": ["git tree", "${TREE_1}"]
    }
  }
}
EOF
cat repo-config.json

echo
echo Build against absent computed root with absent base
echo
"${JUST}" install -o "${OUT}" -C repo-config.json \
          --local-build-root "${LBR}" \
          --remote-serve-address ${SERVE} \
          -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
          --log-limit 4 -f "${LOG}" 2>&1

# sanity check output
[ $(cat "${OUT}/out" | wc -l) -eq 55 ]

echo OK
