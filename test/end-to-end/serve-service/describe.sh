#!/bin/sh
# Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

env

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR1="${TEST_TMPDIR}/local-build-root-1"
readonly LBR2="${TEST_TMPDIR}/local-build-root-1"
readonly OUT="${PWD}/out"
mkdir -p "${OUT}"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

echo Local description
echo
(cd repo \
 && "${JUST}" describe --local-build-root "${LBR1}" > "${OUT}/describe.orig")
cat "${OUT}/describe.orig"
echo

mkdir work
cd work
touch ROOT

cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "git"
      , "commit": "$COMMIT_0"
      , "pragma": {"absent": true}
      , "repository": "http://non-existent.example.org/data.git"
      , "branch": "master"
      , "subdir": "."
      }
    }
  }
}
EOF

echo Repo config
echo
cat repos.json
echo

echo Remotely obtained description
echo
"${JUST_MR}" --norc --local-build-root "${LBR2}" \
             --remote-serve-address ${SERVE} \
             -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
             --log-limit 6 \
             --just "${JUST}" describe > "${OUT}/describe"
cat "${OUT}/describe"
echo

diff -u "${OUT}/describe.orig" "${OUT}/describe"


echo OK
