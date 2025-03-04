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

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly LOG_DIR="${PWD}/log"
readonly ETC_DIR="${PWD}/etc"
readonly WRK_DIR="${PWD}/work"

# Set up an rc file, requesting invocation logging
mkdir -p "${ETC_DIR}"
readonly RC="${ETC_DIR}/rc.json"
cat > "${RC}" <<EOF
{ "invocation log":
  { "directory": {"root": "system", "path": "${LOG_DIR#/}"}
  , "--profile": "profile.json"
  }
, "rc files": [{"root": "workspace", "path": "rc.json"}]
, "just": {"root": "system", "path": "${JUST#/}"}
, "local build root": {"root": "system", "path": "${LBR#/}"}
, "local launcher": ["env", "PATH=${PATH}"]
EOF
if [ -n "${REMOTE_EXECUTION_ADDRESS:-}" ]
then
    if [ -n "${COMPATIBLE:-}" ]
    then
        RC_COMPAT="true"
    else
        RC_COMPAT="false"
    fi
    cat >> "${RC}" <<EOF
, "remote execution": {"address": "${REMOTE_EXECUTION_ADDRESS:-}"
                      , "compatible": ${RC_COMPAT}}
EOF
fi
echo '}' >> "${RC}"
cat "${RC}"

# Setup basic project, setting project id
mkdir -p "${WRK_DIR}"
cd "${WRK_DIR}"
touch ROOT
cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": "."}}}}
EOF
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "arguments_config": ["SLEEP"]
  , "outs": ["done"]
  , "cmds":
    [ { "type": "join_cmd"
      , "$1":
        [ "sleep"
        , { "type": "json_encode"
          , "$1": {"type": "var", "name": "SLEEP", "default": 1}
          }
        ]
      }
    , "touch done"
    ]
  }
}
EOF

# Build for 3 seconds; abuse the project id to distinguish runs
cat > rc.json <<'EOF'
{"invocation log": {"project id": "3s"}}
EOF
"${JUST_MR}" --rc "${RC}" -D '{"SLEEP": 3}' build 2>&1
INVOCATION_DIR="$(ls -d "${LOG_DIR}"/3s/*)"
PROFILE="${INVOCATION_DIR}/profile.json"
cat "${PROFILE}"
# we expect non cached, and an action duration at least 3s
[ $(jq '.actions | .[] | .cached' "${PROFILE}") = "false" ]
[ "$(jq '.actions | .[] | .duration | . >= 3' "${PROFILE}")" = "true" ]


# Build for 3 seconds, again; abuse the project id to distinguish runs
cat > rc.json <<'EOF'
{"invocation log": {"project id": "3s-again"}}
EOF
"${JUST_MR}" --rc "${RC}" -D '{"SLEEP": 3}' build 2>&1
INVOCATION_DIR="$(ls -d "${LOG_DIR}"/3s-again/*)"
PROFILE="${INVOCATION_DIR}/profile.json"
cat "${PROFILE}"
# we expect cached and no duration specified
[ $(jq '.actions | .[] | .cached' "${PROFILE}") = "true" ]
[ $(jq '.actions | .[] | .duration' "${PROFILE}") = "null" ]


# Build for 4 seconds; abuse the project id to distinguish runs
cat > rc.json <<'EOF'
{"invocation log": {"project id": "4s"}}
EOF
"${JUST_MR}" --rc "${RC}" -D '{"SLEEP": 4}' build 2>&1
INVOCATION_DIR="$(ls -d "${LOG_DIR}"/4s/*)"
PROFILE="${INVOCATION_DIR}/profile.json"
cat "${PROFILE}"
# we expect non cached, and an action duration at least 4s
[ $(jq '.actions | .[] | .cached' "${PROFILE}") = "false" ]
[ "$(jq '.actions | .[] | .duration | . >= 4' "${PROFILE}")" = "true" ]

echo OK
