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
{ "flex file":
  { "type": "generic"
  , "arguments_config": ["FLEX_FILE"]
  , "outs": [{"type": "var", "name": "FLEX_FILE"}]
  , "cmds":
    [ { "type": "join"
      , "$1": ["echo content > ", {"type": "var", "name": "FLEX_FILE"}]
      }
    ]
  }
, "flex dir":
  { "type": "generic"
  , "arguments_config": ["FLEX_DIR"]
  , "out_dirs": [{"type": "var", "name": "FLEX_DIR"}]
  , "cmds":
    [ { "type": "join_cmd"
      , "$1": ["mkdir", "-p", {"type": "var", "name": "FLEX_DIR"}]
      }
    , { "type": "join"
      , "$1":
        ["echo content > ", {"type": "var", "name": "FLEX_DIR"}, "/foo.txt"]
      }
    , { "type": "join"
      , "$1":
        ["echo content > ", {"type": "var", "name": "FLEX_DIR"}, "/bar.txt"]
      }
    ]
  }
, "":
  { "type": "generic"
  , "outs": ["ls.txt"]
  , "cmds": ["touch ls.txt", "find . -type f | sort > ls.txt"]
  , "deps": ["flex file", "flex dir"]
  }
}
EOF

cat > rc.json <<'EOF'
{"invocation log": {"project id": "good-build"}}
EOF

"${JUST_MR}" --rc "${RC}" build -p \
  -D '{"FLEX_DIR": "some/dir", "FLEX_FILE": "path/to/file.txt"}' 2>&1
INVOCATION_DIR="$(ls -d "${LOG_DIR}"/good-build/*)"
PROFILE="${INVOCATION_DIR}/profile.json"
cat "${PROFILE}"
# exit code of just should be reported as 0
[ $(jq '."exit code"' "${PROFILE}") -eq 0 ]
# There should be no analysis errors
[ $(jq '."analysis errors" | length' "${PROFILE}") -eq 0 ]


cat > rc.json <<'EOF'
{"invocation log": {"project id": "analysis"}}
EOF

"${JUST_MR}" --rc "${RC}" build -p \
  -D '{"FLEX_DIR": "path/to/dir", "FLEX_FILE": "path/to/dir/file.txt"}' 2>&1 \
  && exit 1 || :
INVOCATION_DIR="$(ls -d "${LOG_DIR}"/analysis/*)"
PROFILE="${INVOCATION_DIR}/profile.json"
cat "${PROFILE}"
# exit code of just should be reported as 8 (analysis)
[ $(jq '."exit code"' "${PROFILE}") -eq 8 ]
# we expect one entry in analysis errors
[ $(jq '."analysis errors" | length' "${PROFILE}") -eq 1 ]
# ... and the error message should describe the problem
jq -r '."analysis errors"[0]' "${PROFILE}" | grep 'stag.*conflict.*path/to/dir'

echo OK
