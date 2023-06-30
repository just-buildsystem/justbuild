#!/bin/sh
# Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly RULES_DIR="${PWD}/src/rules"
readonly LOCAL_CACHE="${TMPDIR}/local_cache"
readonly REMOTE_CACHE="${TMPDIR}/remote_cache"
readonly INFOFILE="${TMPDIR}/info.json"
readonly PIDFILE="${TMPDIR}/pid.txt"

ARGS=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  ARGS="--compatible"
fi

# ------------------------------------------------------------------------------
# Start local remote execution server
# ------------------------------------------------------------------------------
"${JUST}" execute --info-file "${INFOFILE}" --pid-file "${PIDFILE}" \
  --local-build-root "${REMOTE_CACHE}" ${ARGS} 2>&1 &

for _ in `seq 1 10`; do
  if test -f "${INFOFILE}"; then
    break
  fi
  sleep 1;
done

if ! test -f "${INFOFILE}"; then
  echo "Did not find ${INFOFILE}"
  exit 1
fi

readonly PORT=$(jq '."port"' "${INFOFILE}")

cleanup() {
  kill $(cat "${PIDFILE}")
}
trap cleanup EXIT

# ------------------------------------------------------------------------------
# Test synchronization of artifacts in the 'artifacts' and 'runfiles' maps
# ------------------------------------------------------------------------------
cd greetlib
sed -i "s|<RULES_PATH>|${RULES_DIR}|" repos.json

# Build greetlib remotely
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LOCAL_CACHE}" \
  build ${ARGS} -r localhost:${PORT} main 2>&1

# Clear remote cache
rm -rf "${REMOTE_CACHE}"

# Build greetlib remotely
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LOCAL_CACHE}" \
  build ${ARGS} -r localhost:${PORT} main 2>&1

# ------------------------------------------------------------------------------
# Test synchronization of artifacts in the 'provides' map
# ------------------------------------------------------------------------------
cd ../pydicts

# Build pydicts remotely
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LOCAL_CACHE}" \
  build ${ARGS} -r localhost:${PORT} json_from_py 2>&1

# Clear remote cache
rm -rf "${REMOTE_CACHE}"

# Build pydicts remotely
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LOCAL_CACHE}" \
  build ${ARGS} -r localhost:${PORT} json_from_py 2>&1
