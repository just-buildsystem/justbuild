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
{ "upper":
  { "type": "generic"
  , "deps": ["data.txt"]
  , "outs": ["upper.txt"]
  , "cmds":
    [ "cat data.txt | tr a-z A-Z > upper.txt"
    , "echo StdOuT"
    , "echo StdErR 1>&2"
    ]
  }
}
EOF
echo blablabla > data.txt

# Call analyse via just-mr; abuse the project id to distingush the runs
cat > rc.json <<'EOF'
{"invocation log": {"project id": "first-run"}}
EOF
"${JUST_MR}" --rc "${RC}" build upper 2>&1
INVOCATION_DIR="$(ls -d "${LOG_DIR}"/first-run/*)"
PROFILE="${INVOCATION_DIR}/profile.json"

cat "${PROFILE}"
[ $(jq '.actions | .[] | .cached' "${PROFILE}") = "false" ]

OUT_ARTIFACT=$(jq -r '.actions | .[] | .artifacts."upper.txt"' "${PROFILE}")
"${JUST_MR}" --rc "${RC}" install-cas -o "${OUT}/upper.txt" "${OUT_ARTIFACT}" 2>&1
grep BLABLABLA "${OUT}/upper.txt"

STDOUT=$(jq -r '.actions | .[] | .stdout' "${PROFILE}")
STDERR=$(jq -r '.actions | .[] | .stderr' "${PROFILE}")
"${JUST_MR}" --rc "${RC}" install-cas -o "${OUT}/stdout" "${STDOUT}" 2>&1
"${JUST_MR}" --rc "${RC}" install-cas -o "${OUT}/stderr" "${STDERR}" 2>&1
grep StdOuT "${OUT}/stdout"
grep StdErR "${OUT}/stderr"
echo

# Build again, this time the action should be cached;
# again abuse the project id to distingush the runs
cat > rc.json <<'EOF'
{"invocation log": {"project id": "second-run"}}
EOF
"${JUST_MR}" --rc "${RC}" build upper 2>&1
INVOCATION_DIR="$(ls -d "${LOG_DIR}"/second-run/*)"
PROFILE="${INVOCATION_DIR}/profile.json"

cat "${PROFILE}"
[ $(jq '.actions | .[] | .cached' "${PROFILE}") = "true" ]

echo OK
