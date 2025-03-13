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

# Demonstrate the use case of analysing a tree available in CAS by means of
# serve without fetching it.

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR_COLLECT="${TEST_TMPDIR}/lbr-collect"
readonly LBR_ANALYSE="${PWD}/lbr-analyse" # Keep as build artifact
readonly DATA_CREATION="${TEST_TMPDIR}/data-create"
readonly WRKDIR="${PWD}/work"
readonly OUT="${PWD}/out"

# Somewhere, data is created and uploaded to the remote CAS
mkdir -p "${DATA_CREATION}"
cd "${DATA_CREATION}"
for i in `seq 0 19`
do
    echo 0 > $((4 * i)).dat
    echo 1 > $((4 * i + 1)).dat
    echo 0 > $((4 * i + 2)).dat
    echo 0 > $((4 * i + 3)).dat
done
echo "There NO reason whatsoever, that this file occurs bei by AcCiDeNt in local build root" > magic.txt
DATA_TREE=$("${JUST}" add-to-cas --local-build-root "${LBR_COLLECT}" \
                      -r "${REMOTE_EXECUTION_ADDRESS}" .)
MAGIC=$("${JUST}" add-to-cas --local-build-root "${LBR_COLLECT}" \
                      -r "${REMOTE_EXECUTION_ADDRESS}" magic.txt)

echo "Data tree is ${DATA_TREE}, it contains a file with hash ${MAGIC}"

# Locally we analyse the tree without fetching it
mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > rc.json <<EOF
{ "local build root": {"root": "system", "path": "${LBR_ANALYSE}"}
, "just": {"root": "system", "path": "${JUST}"}
, "remote serve": {"address": "${SERVE}"}
, "remote execution": {"address": "${REMOTE_EXECUTION_ADDRESS}"}
}
EOF
cat rc.json
cat > repos.json <<EOF
{ "repositories":
  { "data":
    { "repository":
      { "type": "git tree"
      , "id": "${DATA_TREE}"
      , "cmd": ["false", "Should be known to CAS"]
      , "pragma": {"absent": true}
      }
    }
  , "data targets":
    { "repository":
      {"type": "file", "path": "targets", "pragma": {"to_git": true}}
    }
  , "tools":
    { "repository":
      {"type": "file", "path": "tools", "pragma": {"to_git": true}}
    }
  , "":
    { "repository": "data"
    , "target_root": "data targets"
    , "bindings": {"tools": "tools"}
    }
  }
}
EOF
cat repos.json

mkdir tools
echo {} > tools/TARGETS
cat > tools/stats.py <<'EOF'
#!/usr/bin/env python3

import json
import os

count = 0
sum = 0.0
sum_squares = 0.0

for root, dirs, files in os.walk('data'):
    for fname in files:
        if fname.endswith(".dat"):
          with open(os.path.join('data', fname)) as f:
            data = f.read()
            value = float(data)
            count += 1
            sum += value
            sum_squares += value * value

result = {"count": count,
          "sum x": sum,
          "sum x^2": sum_squares}

with open("stats.json", "w") as f:
    json.dump(result, f)
EOF
chmod 755 tools/stats.py

mkdir targets
cat > targets/TARGETS <<'EOF'
{ "": {"type": "export", "target": "stats"}
, "stats":
  { "type": "generic"
  , "outs": ["stats.json"]
  , "cmds": ["./stats.py"]
  , "deps": ["data", ["@", "tools", "", "stats.py"]]
  }
, "data": {"type": "install", "dirs": [[["TREE", null, "."], "data"]]}
}
EOF

"${JUST_MR}" --rc rc.json install -o "${OUT}" 2>&1

# Sanity check the result we obtained
echo
cat "${OUT}/stats.json"
echo
[ $(jq '.count | . == 80' "${OUT}/stats.json") = "true" ]
[ $(jq '."sum x" | . == 20' "${OUT}/stats.json") = "true" ]
[ $(jq '."sum x^2" | . == 20' "${OUT}/stats.json") = "true" ]
# Verify that the root is not downloaded
echo
echo "Verifying ${MAGIC} is not known locally"
"${JUST}" install-cas --local-build-root "${LBR_ANALYSE}" "${MAGIC}" 2>&1 && exit 1 || :
echo

echo OK
