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

env

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBRA="${TEST_TMPDIR}/local-build-root-a"
readonly LBRB="${TEST_TMPDIR}/local-build-root-b"
readonly LBRC="${TEST_TMPDIR}/local-build-root-c"
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

"${JUST_MR}" --norc --local-build-root "${LBRA}" \
             --remote-serve-address ${SERVE} \
             -f "${OUT}/build.log" \
             -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
             --just "${JUST}" \
             install --remember -o "${OUT}/result" \
             -D '{"DEPTH": "900"}' \
             --dump-artifacts artifacts.json \
             2>&1

# Santity check: we find data2.txt and verify the content
find "${OUT}/result" -name data2.txt 2>&1
grep 2 $(find "${OUT}/result" -name data2.txt)


# Verify that the deep tree can also be handled by install-cas
# with and without the --remember option
cat artifacts.json
echo

"${JUST_MR}" --norc --local-build-root "${LBRB}" \
             -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
             --just "${JUST}" \
             install-cas --remember -o "${OUT}/first-copy" \
             $(jq -r '."'"${OUT}/result/out"'".id'  artifacts.json)::t \
             2>&1

"${JUST_MR}" --norc --local-build-root "${LBRC}" \
             -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
             --just "${JUST}" \
             install-cas -o "${OUT}/second-copy" \
             $(jq -r '."'"${OUT}/result/out"'".id'  artifacts.json)::t \
             2>&1

# sanity check of the copies
grep 2 $(find "${OUT}/first-copy" -name data2.txt)
grep 2 $(find "${OUT}/second-copy" -name data2.txt)

echo OK
