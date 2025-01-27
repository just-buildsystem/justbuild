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

readonly LBR="${TEST_TMPDIR}/local-build-root"

readonly FILE_ROOT="${TEST_TMPDIR}/file-root"
readonly WRKDIR="${PWD}/work"

# Set up file repo
mkdir -p "${FILE_ROOT}/test_dir"
echo test > "${FILE_ROOT}/test_dir/test_file"
ln -s test_file "${FILE_ROOT}/test_dir/test_link"

# Main repository
mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > repos.in.json <<EOF
{ "repositories":
  { "file_repo":
    { "repository":
      { "type": "file"
      , "path": "${FILE_ROOT}/test_dir"
      , "pragma": {"to_git": true}
      }
    }
  }
}
EOF
echo
echo Input config:
cat repos.in.json
echo

echo Clone repo:
CLONE_TO="cloned_foo"
"${JUST_LOCK}" -C repos.in.json -o repos.json \
               --clone '{"'${CLONE_TO}'": ["file_repo", []]}' \
               --local-build-root "${LBR}" 2>&1
echo
echo Output config:
cat repos.json
echo

echo Check cloned entries:
[ -f "${CLONE_TO}/test_file" ] && [ -L "${CLONE_TO}/test_link" ] || exit 1
grep test "${CLONE_TO}/test_file"
readlink -m "${CLONE_TO}/test_link" | grep test_file
echo

echo Check output configuration:
grep "${CLONE_TO}" repos.json
grep "pragma" repos.json
echo

echo "OK"
