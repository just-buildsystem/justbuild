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

readonly JUST_LOCK="${PWD}/bin/lock-tool-under-test"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"

readonly LOCK_LBR="${TEST_TMPDIR}/local-build-root"
readonly LBR_1="${TEST_TMPDIR}/local-build-root-1"
readonly LBR_2="${TEST_TMPDIR}/local-build-root-2"

readonly SCRATCH_LBR="${TEST_TMPDIR}/scratch-local-build-root"
readonly SCRATCH="${TEST_TMPDIR}/scratch-space"

readonly ROOT=`pwd`
readonly BIN="${TEST_TMPDIR}/bin"
readonly WRKDIR="${TEST_TMPDIR}/work"

# Set up git tree
mkdir -p "${BIN}"
cat > "${BIN}/mock-vcs" <<'EOF'
#!/bin/sh

mkdir -p foo/bar/baz
echo foo data > foo/data.txt
echo bar data > foo/bar/data.txt
echo baz data > foo/bar/baz/data.txt
EOF
chmod 755 "${BIN}/mock-vcs"
export PATH="${BIN}:${PATH}"
rm -rf "${SCRATCH}"
mkdir -p "${SCRATCH}"
(cd "${SCRATCH}" && "${BIN}/mock-vcs")
FOREIGN_GIT_TREE=$("${JUST}" add-to-cas --local-build-root "${SCRATCH_LBR}" "${SCRATCH}")

# Input configuration file
mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > repos.in.json <<EOF
{ "repositories":
  { "git_tree":
    { "repository":
      { "type": "git tree"
      , "id": "${FOREIGN_GIT_TREE}"
      , "cmd": ["${BIN}/mock-vcs"]
      , "inherit env": ["PATH"]
      }
    }
  }
}
EOF
echo
echo Input config:
cat repos.in.json
echo

# Check initial setup
CONF=$("${JUST_MR}" -L '["env", "PATH='"${PATH}"'"]' --norc --just "${JUST}" \
                    -C repos.in.json --local-build-root "${LBR_1}" setup) 2>&1
echo

[ "$(jq '."repositories"."git_tree"."workspace_root" | .[1]' "${CONF}")" \
    = \""${FOREIGN_GIT_TREE}"\" ]

echo Clone repo:
CLONE_TO="cloned_foo"
"${JUST_LOCK}" -C repos.in.json -o repos.json --local-build-root "${LOCK_LBR}" \
               --clone '{"'${CLONE_TO}'": ["git_tree", []]}' 2>&1
echo
echo Output config:
cat repos.json
echo

echo Check output configuration:
grep "${CLONE_TO}" repos.json
echo

# Check setup with local clones:
"${JUST_MR}" -L '["env", "PATH='"${PATH}"'"]' --norc --just "${JUST}" \
             -C repos.json --local-build-root "${LBR_2}" setup 2>&1
echo

# Check clone location has the expected content
[ $("${JUST}" add-to-cas --local-build-root "${LBR_2}" "${CLONE_TO}") \
    = "${FOREIGN_GIT_TREE}" ]

echo "OK"
