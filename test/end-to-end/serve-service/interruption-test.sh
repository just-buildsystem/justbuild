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

pids="" # use string instead of array for portability as arrays are an extension
killpids=""
echo Starting processes expected to finish
echo

for i in `seq 1 4`
do echo Starting build with parameter $i
"${JUST_MR}" --norc --local-build-root "${LBR}" \
             --remote-serve-address ${SERVE} \
             -f "${OUT}/proc$i.log" \
             --restrict-stderr-log-limit 1 \
             -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
             --just "${JUST}" \
             build -D '{"RANGE": "`seq 1 '"${i}"'`"}' \
             2>&1 &
pid="$!"
pids="${pids} ${pid}"
done
echo
echo Starting processes expected to be interrupted
echo
for i in `seq 20 35`
do echo Starting build with parameter $i
"${JUST_MR}" --norc --local-build-root "${LBR}" \
             --remote-serve-address ${SERVE} \
             -f "${OUT}/proc$i.log" \
             --restrict-stderr-log-limit 1 \
             -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
             --just "${JUST}" \
             build -D '{"RANGE": "`seq 1 '"${i}"'`"}' \
             2>&1 &
pid="$!"
pids="${pids} ${pid}"
killpids="${killpids} ${pid}"
done
echo "Started ${pids}"

sleep 7
echo "Stopping processes ${killpids}"
for pid in ${killpids}; do
    kill $pid
done
echo "Waiting for processes ${pids}"
for pid in ${pids}; do
    wait $pid || echo "$pid has exit code $?"
done
echo done
echo

# Serve should not be affected by clients disappearing and we
# still should be able to build, both targets already requested
# as well as new ones.
"${JUST_MR}" --norc --local-build-root "${LBR}" \
             --remote-serve-address ${SERVE} \
             -f "${OUT}/finalout3.log" \
             -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
             --just "${JUST}" \
             install -o "${OUT}/finalout3" -D '{"RANGE": "`seq 1 3`"}' 2>&1
echo
"${JUST_MR}" --norc --local-build-root "${LBR}" \
             --remote-serve-address ${SERVE} \
             -f "${OUT}/finalout7.log" \
             -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
             --just "${JUST}" \
             install -o "${OUT}/finalout7" -D '{"RANGE": "`seq 1 7`"}' 2>&1
echo
echo Sanity checks
grep 2 "${OUT}/finalout7/out.txt"
grep 6 "${OUT}/finalout7/out.txt"
grep 2 "${OUT}/finalout3/out.txt"
grep 6 "${OUT}/finalout3/out.txt" && exit 1 || :
echo
echo OK
