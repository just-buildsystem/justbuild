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

if [ "${COMPATIBLE:-}" = "YES" ]; then
  ARGS="--compatible"
  HASH_TYPE="compatible-sha256"
  TREE_TAG="f"
else
  ARGS=""
  HASH_TYPE="git-sha1"
  TREE_TAG="t"
fi
readonly FIRST_GEN="${LOCAL_CACHE}/protocol-dependent/generation-0/$HASH_TYPE"
readonly TCDIR="$FIRST_GEN/tc"

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

check_main_blobs() {
  for entry in "${TCDIR}"/*
  do
    for sbdir in "${entry}"/*
    do
      for f in "${sbdir}"/*
      do
        hash=$(cat $f)
        TC_ENTRY=$("$JUST" install-cas --local-build-root "${LOCAL_CACHE}" $ARGS ${hash})
        FILE_HASH=${FILE_HASH:-$(echo $TC_ENTRY | jq -r '.artifacts."libgreet.a".data.id // ""')}
        TREE_HASH=${TREE_HASH:-$(echo $TC_ENTRY | jq -r '.runfiles.greet.data.id // ""')}
        "$JUST" install-cas --local-build-root "${LOCAL_CACHE}" ${ARGS} ${FILE_HASH}::f > /dev/null
        "$JUST" install-cas --local-build-root "${LOCAL_CACHE}" ${ARGS} ${TREE_HASH}::${TREE_TAG} > /dev/null
      done
    done
  done
}

# ------------------------------------------------------------------------------
# Test synchronization of artifacts in the 'artifacts' and 'runfiles' maps
# ------------------------------------------------------------------------------
cd greetlib
sed -i "s|<RULES_PATH>|${RULES_DIR}|" repos.json


# Build greetlib remotely
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LOCAL_CACHE}" \
  build ${ARGS} -r localhost:${PORT} --dump-graph graph.json main 2>&1

# Count actions without tc
EXPECTED=4
readonly ACTIONS_NO_TC=$(cat graph.json | jq '.actions | length' )

test ${ACTIONS_NO_TC} -eq ${EXPECTED} || printf "Wrong number of actions. %d were expected but found %d\n" ${EXPECTED} ${ACTIONS_NO_TC} > /dev/stderr

# Check the existence of target-cache dir
test -d ${TCDIR}

check_main_blobs

# Clear remote cache
rm -rf "${REMOTE_CACHE}"

# Build greetlib remotely
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LOCAL_CACHE}" \
  build ${ARGS} -r localhost:${PORT} --dump-graph graph-tc.json main 2>&1

# Count actions with tc
readonly ACTIONS_TC=$(cat graph-tc.json | jq '.actions | length' )

EXPECTED=2
test ${ACTIONS_TC} -eq ${EXPECTED} || printf "Wrong number of actions. %d were expected but found %d\n" ${EXPECTED} ${ACTIONS_TC} > /dev/stderr

# ------------------------------------------------------------------------------
# Test synchronization of artifacts in the 'provides' map
# ------------------------------------------------------------------------------
cd ../pydicts

rm -rf "${REMOTE_CACHE}"

# Build pydicts remotely
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LOCAL_CACHE}" \
  build ${ARGS} -r localhost:${PORT} json_from_py 2>&1

# Clear remote cache
rm -rf "${REMOTE_CACHE}"

# Build pydicts remotely
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LOCAL_CACHE}" \
  build ${ARGS} -r localhost:${PORT} json_from_py 2>&1
