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
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${PWD}/out"
mkdir -p "${OUT}"


COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

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

echo  Remotely failing build
echo
"${JUST_MR}" --norc --local-build-root "${LBR}" \
             --remote-serve-address ${SERVE} \
             -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
             --just "${JUST}" \
             build -f  "${OUT}/log" --serve-errors-log "${OUT}/serve.log" 2>&1 \
             && exit 1 || :
echo
cat "${OUT}/serve.log"
echo
SERVE_LOG=$(jq -rM '.[0][1]' "${OUT}/serve.log")
echo Serve log has blob ${SERVE_LOG}
echo
# The serve log blob must be mentioned in the log file
grep ${SERVE_LOG} "${OUT}/log"
echo
# Fetch the failure log
"${JUST_MR}" --norc --local-build-root "${LBR}" \
             --remote-serve-address ${SERVE} \
             -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
             --just "${JUST}" \
             install-cas -o "${OUT}/failure.log" ${SERVE_LOG} 2>&1
echo
cat "${OUT}/failure.log"
echo
# Failure log must contain error message
grep FaIlUrEmEsSaGe "${OUT}/failure.log"
# Failure log must contain exit code
grep 42 "${OUT}/failure.log"
# Failure log must contain failing targets
grep FaIlInGtArGeT "${OUT}/failure.log"

echo
echo OK
