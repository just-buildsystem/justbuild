#!/bin/sh
# Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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


readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly TOOLS_DIR="${TEST_TMPDIR}/tools"
readonly COMPUTE_TREE_DIR="${TEST_TMPDIR}/generate-the-tree-id"
readonly LBR="${TEST_TMPDIR}/local-build-root"

# Set up a tree generating tool, and the corresponding tree

readonly TOOL_MSG_A="UsefulTextOnStout"
readonly TOOL_MSG_B="PotentialErrorOnStderr"
mkdir -p "${TOOLS_DIR}"
readonly TOOL="${TOOLS_DIR}/generate-tree.sh"
cat > "${TOOL}" <<EOF
#!/bin/sh
echo "${TOOL_MSG_A}"
echo "${TOOL_MSG_B}" 1>&2
echo {} > TARGETS
if [ -n "\${SHOULD_FAIL:-}" ]
then
  exit 0
fi
echo data > foo.txt
EOF
chmod 755 "${TOOL}"
mkdir -p "${COMPUTE_TREE_DIR}"
( cd "${COMPUTE_TREE_DIR}"
  git init
  git config user.email "nobody@example.org"
  git config user.name "Nobody"
  "${TOOL}"
  git add .
  git commit -m "Sample output"
)
readonly TREE_ID=$(cd "${COMPUTE_TREE_DIR}" && git log -n 1 --format="%T")
rm -rf "${COMPUTE_TREE_DIR}"

# Set up workspace

touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      {"type": "git tree", "id": "${TREE_ID}", "cmd": ["${TOOL}"]}
    }
  }
}
EOF

# On failure, the messages should be shown on stderr,
# as well as the effective command

echo
echo testing setup failure
echo

"${JUST_MR}" --local-build-root "${LBR}" \
             -L '["env", "SHOULD_FAIL=YES"]' setup 2>log && exit 1 || :
cat log
grep -q -F "${TOOL_MSG_A}" log
grep -q -F "${TOOL_MSG_B}" log
grep -q -F "SHOULD_FAIL=YES" log
grep -q -F "${TOOL}" log

# On success, everything should be quiet (despite the command
# potentially producing output)

echo
echo testing setup success
echo

CONF=$("${JUST_MR}" --local-build-root "${LBR}" \
       -L '["env", "SHOULD_FAIL="]' setup 2>log)
cat log
echo "${CONF}"
cat "${CONF}"
echo
grep -F "${TOOL_MSG_A}" log && exit 1 || :
grep -F "${TOOL_MSG_B}" log && exit 1 || :
grep -F "${TOOL}" log && exit 1 || :
# sanity check: the generated configuration contains the requested tree
grep -q -F "${TREE_ID}" "${CONF}"

echo OK
