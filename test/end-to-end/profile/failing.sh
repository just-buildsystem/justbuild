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
readonly OUT="${TEST_TMPDIR}/out"

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
}
EOF
cat "${RC}"

# Setup basic project, setting project id
mkdir -p "${WRK_DIR}"
cd "${WRK_DIR}"
touch ROOT
cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": "."}}}}
EOF


cat > TARGETS <<'EOF'
{ "will-fail":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds":
    [ "echo StdOuT"
    , "echo StdErR 1>&2"
    , "exit 42"
    ]
  }
}
EOF

cat > rc.json <<'EOF'
{"invocation log": {"project id": "failing"}}
EOF

"${JUST_MR}" --rc "${RC}" build will-fail 2>&1 && exit 1 || :
INVOCATION_DIR="$(ls -d "${LOG_DIR}"/failing/*)"
PROFILE="${INVOCATION_DIR}/profile.json"
cat "${PROFILE}"

# exit code of just should be reported as 1
[ $(jq '."exit code"' "${PROFILE}") -eq 1 ]
# exit code of the single action should be reported as 42
[ $(jq -r '.actions | .[] | ."exit code"' "${PROFILE}" 2>&1) -eq 42 ]

# stdout and stderr of the single action should be reported correctly
STDOUT=$(jq -r '.actions | .[] | .stdout' "${PROFILE}")
STDERR=$(jq -r '.actions | .[] | .stderr' "${PROFILE}")
"${JUST_MR}" --rc "${RC}" install-cas -o "${OUT}/stdout" "${STDOUT}" 2>&1
"${JUST_MR}" --rc "${RC}" install-cas -o "${OUT}/stderr" "${STDERR}" 2>&1
grep StdOuT "${OUT}/stdout"
grep StdErR "${OUT}/stderr"
echo

echo OK
