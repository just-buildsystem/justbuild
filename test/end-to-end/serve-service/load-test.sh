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

pids="" # use string instead of array for portability as arrays are an extension
for i in `seq 1 2`
do "${JUST_MR}" --norc --local-build-root "${LBR}" \
                --remote-serve-address ${SERVE} \
                -f "${OUT}/build${i}.log" \
                --restrict-stderr-log-limit 1 \
                -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
                --just "${JUST}" \
                install -o "${OUT}/out${i}" 2>&1 &
pid="$!"
pids="${pids} ${pid}"
done
echo "Waiting for processes ${pids}"
for pid in ${pids}; do
    wait $pid || echo "$pid has exit code $?"
done
echo done
echo

# Sanity check: files contain some numbers
for i in `seq 1 2`
do grep 123 "${OUT}/out${i}/deep"
   grep 123 "${OUT}/out${i}/wide"
done

echo
echo OK
