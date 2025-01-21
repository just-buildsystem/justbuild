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

ROOT="$(pwd)"
JUST="${ROOT}/bin/tool-under-test"
JUST_MR="${ROOT}/bin/mr-tool-under-test"
BUILDROOT="${TEST_TMPDIR}/build-root"
OUT="${TEST_TMPDIR}/out"
WRKDIR="${ROOT}/work"
SRCDIR="${ROOT}/srcs"

# Add a src git repo
mkdir -p "${SRCDIR}"
cd "${SRCDIR}"
mkdir -p src/with/some/deep/directory
echo 'Hello World' > src/with/some/deep/directory/hello.txt

# Set up a build root from git repo
mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      {"type": "file", "path": "${SRCDIR}", "pragma": {"to_git": true}}
    }
  }
}
EOF
cat repos.json
echo
"${JUST_MR}" --norc --local-build-root "${BUILDROOT}" setup > CONF
echo
cat CONF
echo
cat $(cat CONF)
echo
TREE=$(jq -rM '.repositories."".workspace_root[1]' $(cat CONF))
echo
echo Tree is ${TREE}
echo

# Clear local cas, while keeping repositories (and hence git cas)
"${JUST}" gc --local-build-root "${BUILDROOT}" 2>&1
"${JUST}" gc --local-build-root "${BUILDROOT}" 2>&1

# Verify that we can access the tree (via the git cas),
# even if a remote endpoint is provided.
"${JUST}" install-cas --local-build-root "${BUILDROOT}" \
          -r  "${REMOTE_EXECUTION_ADDRESS}" "${TREE}::t" -o "${OUT}" 2>&1
grep World "${OUT}/src/with/some/deep/directory/hello.txt"

echo
echo OK
