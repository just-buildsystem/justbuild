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

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly DATA_DIR="${PWD}/foreign-file-data"
readonly OUT="${TEST_TMPDIR}/out"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

ENDPOINT_ARGS="-r ${REMOTE_EXECUTION_ADDRESS} -R ${SERVE} ${COMPAT}"
echo
echo Will use endpoint arguments ${ENDPOINT_ARGS}

DATA_HASH=$(git hash-object "${DATA_DIR}/data.txt")
TARGET_HASH=$(git hash-object "${DATA_DIR}/the-targets-file")

mkdir work
cd work
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "data":
    { "repository":
      { "type": "foreign file"
      , "content": "${DATA_HASH}"
      , "fetch": "http://non-existent.example.org/content.txt"
      , "name": "hello.txt"
      , "pragma": {"absent": true}
      }
    }
  , "target":
    { "repository":
      { "type": "foreign file"
      , "content": "${TARGET_HASH}"
      , "fetch": "http://non-existent.example.org/target.txt"
      , "name": "TARGETS"
      , "pragma": {"absent": true}
      }
    }
  , "": {"repository": "data", "target_root": "target"}
  }
}
EOF
echo
cat repos.json
echo


# verify we can build

mkdir -p "${OUT}"

"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
             ${ENDPOINT_ARGS} \
             install -o "${OUT}" 2>&1

grep 'HELLO WORLD' "${OUT}/out.txt"

# also verify that the repo config has the repository absent

CONF=$("${JUST_MR}" --norc --local-build-root "${LBR}" ${ENDPOINT_ARGS} setup)
echo
echo Configuration used was ${CONF}
echo
cat "${CONF}"
echo

test $(jq '.repositories.""."workspace_root" | length' "${CONF}") -eq 2

echo OK
