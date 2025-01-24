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

set -e

readonly ROOT="$(pwd)"
readonly LBRDIR="${TMPDIR}/local-build-root"
readonly JUST="${ROOT}/bin/tool-under-test"
readonly JUST_MR="${ROOT}/bin/mr-tool-under-test"
readonly MAIN="${ROOT}/main-repo"
readonly OTHER="${ROOT}/other-repo"


mkdir -p "${OTHER}"
cd "${OTHER}"
echo {} > TARGETS
echo other context > data


mkdir -p "${MAIN}"
cd "${MAIN}"
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "${MAIN}"}
    , "bindings": {"other": "other"}
    }
  , "other": {"repository": {"type": "file", "path": "${MAIN}"}}
  }
}
EOF
cat repos.json
echo

echo my content > data
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["out"]
  , "cmds": ["cat data > out"]
  , "deps": [["@", "other", "", "data"], ["TREE", null, "."]]
  }
}
EOF

echo
echo local file should analyse fine
echo
"${JUST_MR}" --just "${JUST}" --local-build-root "${LBRDIR}" \
             analyse data 2>&1

echo
echo The default target should detect a staging conflict
echo
"${JUST_MR}" --just "${JUST}" --local-build-root "${LBRDIR}" \
             analyse -f log --dump-actions - 2>&1 && exit 1 || :

echo
# Verify that the error message is related
grep -i error log
grep -i conflict log

echo
echo OK
