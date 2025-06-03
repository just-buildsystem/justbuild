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

###
# This test checks that roots with upwards symlinks can be added to the Git CAS,
# but referencing invalid entries fails in analysis.
##

set -eu

env

readonly ROOT="$(pwd)"

readonly JUST="${ROOT}/bin/tool-under-test"
readonly JUST_MR="${ROOT}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/out"
readonly SRCDIR="${ROOT}/src"
readonly WRKDIR="${ROOT}/work"

# Set up data with a valid symlink
mkdir -p "${SRCDIR}/data-subtree" "${SRCDIR}/links-subtree"
touch "${SRCDIR}/data-subtree/something"
ln -s NON_EXISTENT "${SRCDIR}/links-subtree/valid"  # valid symlink
cat > "${SRCDIR}/TARGETS" << EOF
{ "":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["ls -alR > out.txt"]
  , "deps": [["TREE", null, "data-subtree"], ["TREE", null, "links-subtree"]]
  }
}
EOF

# Main repo depending on repo 'bar'
mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > repos.json << EOF
{ "repositories":
  { "":
    { "repository":
      {"type": "file", "path": "${SRCDIR}", "pragma": {"to_git": true}}
    }
  }
}
EOF

# Test success with valid symlink in links-subtree
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" \
    -L '["env", "PATH='"${PATH}"'"]' install -o ${OUT} 2>&1
echo
cat "${OUT}/out.txt"
echo

# Test analysis failure if invalid (upwards) symlink is added to links-subtree
ln -s ../NON_EXISTENT "${SRCDIR}/links-subtree/invalid"

"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" \
    -L '["env", "PATH='"${PATH}"'"]' analyse 2>&1 \
    && echo "this should fail" && exit 1 || :
echo
echo "failed as expected"

echo OK
