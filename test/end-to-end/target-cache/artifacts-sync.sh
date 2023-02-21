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

# This test tests the synchronization mechanism of target-level-cached remote
# artifacts to the local CAS and back. It requires remote execution and is
# skipped in case of local execution. The test is separated into three phases.
# In phase I, the execution IDs for the local and the remote execution backends
# are determined. In phase II, artifacts mentioned in the 'artifacts' and the
# 'runfiles' map of a target-cache entry are tested. In phase III, artifacts
# mentioned in the 'provides' map of a target-cache entry are tested.

# In order to test the synchronization of target-level-cached remote artifacts
# to the local CAS and back, the following testing technique has been designed.
# The test needs to verify that any artifact that is mentioned in a target-cache
# entry and is garbage collected on the remote side is uploaded again by the
# synchronization mechanism and thus making it available for remote action
# execution. A complete verification needs to test the download (backup)
# direction of the synchronization mechanism and the upload mechanism. Thus
# phase II and III are separated in part A and B to test the download and upload
# accordingly. To test the download direction, a remote execution is triggered
# and artifacts mentioned in the target-cache entry should be available in the
# local CAS. To test the upload direction, we need a garbage collection of some
# backed-up files, which have been downloaded during a remote execution. In
# order to "emulate" the behavior of a garbage-collected file or tree on the
# remote side, a simple project with parts eligible for target-level caching is
# built locally and the target-cache entry is pretended to be created for the
# remote execution backend. If then remote execution is triggered, the files or
# trees are not available on the remote side, so they need to be uploaded from
# the local CAS. Since there are three locations within a target-cache entry
# where artifacts can be mentioned, all three locations need to be tested,
# 'artifacts' and 'runfiles' map in test phase II and 'provides' map in test
# phase III.

# Since this test works with KNOWN artifacts, once an artifact is uploaded to
# the remote side as part of a test execution, it is known with that hash and
# would not cause an error in the next test execution. Thus, we inject random
# strings in the affected source files to create artifacts not yet known to the
# remote side.

readonly ROOT="$PWD"
readonly JUST="$ROOT/bin/tool-under-test"
readonly JUST_MR="$ROOT/foo/bin/just-mr.py"
readonly JUST_RULES="$ROOT/foo/rules"
readonly LBRDIR="$TEST_TMPDIR/local-build-root"
readonly TESTDIR="$TEST_TMPDIR/test-root"
readonly CREDENTIALS_DIR="${PWD}/credentials"

if [ "${REMOTE_EXECUTION_ADDRESS:-}" = "" ]; then
  echo
  echo "Test skipped, since no remote execution is specified."
  echo
  return
fi

REMOTE_EXECUTION_ARGS="-r $REMOTE_EXECUTION_ADDRESS"
if [ "${REMOTE_EXECUTION_PROPERTIES:-}" != "" ]; then
  REMOTE_EXECUTION_ARGS="$REMOTE_EXECUTION_ARGS --remote-execution-property $REMOTE_EXECUTION_PROPERTIES"
fi

AUTH_ARGS=""
if [ -f "${CREDENTIALS_DIR}/ca.crt" ]; then
  AUTH_ARGS=" --tls-ca-cert ${CREDENTIALS_DIR}/ca.crt "
  if [ -f "${CREDENTIALS_DIR}/client.crt" ]; then
    AUTH_ARGS=" --tls-client-cert ${CREDENTIALS_DIR}/client.crt "${AUTH_ARGS}
  fi
  if [ -f "${CREDENTIALS_DIR}/client.key" ]; then
    AUTH_ARGS=" --tls-client-key ${CREDENTIALS_DIR}/client.key "${AUTH_ARGS}
  fi
fi

if [ "${COMPATIBLE:-}" = "YES" ]; then
  ARGS="--compatible"
  HASH_TYPE="compatible-sha256"
  TREE_CAS_DIR="casf"
else
  ARGS=""
  HASH_TYPE="git-sha1"
  TREE_CAS_DIR="cast"
fi
readonly FIRST_GEN="${LBRDIR}/protocol-dependent/generation-0/$HASH_TYPE"
readonly TCDIR="$FIRST_GEN/tc"
readonly EXEC_CAS="$FIRST_GEN/casx"
readonly FILE_CAS="$FIRST_GEN/casf"
readonly TREE_CAS="$FIRST_GEN/$TREE_CAS_DIR"

# Print the CASF hashes of all target cache entries found for a given backend
# (parameter $1)
get_tc_hashes() {
  for FILE in $(find "$TCDIR/$1" -type f); do
    cat "$FILE" | tr -d '[]' | cut -d: -f1
  done
}

# print cache path (ab/cde...) for given hash (parameter $1)
cache_path() {
  echo "$(echo $1 | cut -b -2)/$(echo $1 | cut -b 3-)"
}

# ------------------------------------------------------------------------------
# Test Phase I: Determine local and remote execution ID
# ------------------------------------------------------------------------------

echo
echo "Test phase I"
echo

# Create test files
mkdir -p "$TESTDIR"
cd "$TESTDIR"
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "main":
    {"repository": {"type": "file", "path": ".", "pragma": {"to_git": true}}}
  }
}
EOF

cat > TARGETS <<EOF
{ "main": {"type": "export", "target": ["./", "main-target"]}
, "main-target":
  {"type": "generic", "cmds": ["echo foo > foo.txt"], "outs": ["foo.txt"]}
}
EOF

# Determine local execution ID
"$JUST_MR" --norc --just "$JUST" --local-build-root "$LBRDIR" build main $ARGS
readonly LOCAL_EXECUTION_ID=$(ls -1 "$TCDIR" | head -n1)
echo "Local execution ID: $LOCAL_EXECUTION_ID"
rm -rf "$TCDIR"

# Determine remote execution ID
"$JUST_MR" --norc --just "$JUST" --local-build-root "$LBRDIR" build main $ARGS $REMOTE_EXECUTION_ARGS ${AUTH_ARGS}
readonly REMOTE_EXECUTION_ID=$(ls -1 "$TCDIR" | head -n1)
echo "Remote execution ID: $REMOTE_EXECUTION_ID"
rm -rf "$TCDIR"

# Clean up test files
rm -rf "$TESTDIR" "$LBRDIR"
cd "$ROOT"

# ------------------------------------------------------------------------------
# Test Phase II: Test artifacts sync of 'artifacts' and 'runfiles' map
# ------------------------------------------------------------------------------

echo
echo "Test phase II"
echo

# Copy greetlib test files
cp -r "$ROOT/greetlib" "$TESTDIR"
cd "$TESTDIR"

# Inject rules path into repos.json
sed -i "s|<RULES_PATH>|$JUST_RULES|" repos.json

# A) TEST DOWNLOAD
# ----------------
echo "Check artifacts download"

# Inject random string into source files
RANDOM_STRING=$(hostname).$(date +%s%N).$$
sed -i "s|RANDOM_STRING_1 \".*\"|RANDOM_STRING_1 \"$RANDOM_STRING\"|" greet/include/greet.hpp
sed -i "s|RANDOM_STRING_2 \".*\"|RANDOM_STRING_2 \"$RANDOM_STRING\"|" greet/src/greet.cpp

# Build greetlib remotely
"$JUST_MR" --norc --just "$JUST" --local-build-root "$LBRDIR" --main main build main $ARGS $REMOTE_EXECUTION_ARGS ${AUTH_ARGS}

# Check if file and tree artifacts have been downloaded correctly
EXEC_HASH=
FILE_HASH=
TREE_HASH=
readonly TC_HASHES=$(get_tc_hashes $REMOTE_EXECUTION_ID)
for TC_HASH in $TC_HASHES; do
  TC_ENTRY=$("$JUST" install-cas --local-build-root "$LBRDIR" $ARGS ${TC_HASH})
  EXEC_HASH=${EXEC_HASH:-$(echo $TC_ENTRY | jq -r '.artifacts."main".data.id // ""')}
  FILE_HASH=${FILE_HASH:-$(echo $TC_ENTRY | jq -r '.artifacts."libgreet.a".data.id // ""')}
  TREE_HASH=${TREE_HASH:-$(echo $TC_ENTRY | jq -r '.runfiles.greet.data.id // ""')}
done
test -x ${EXEC_CAS}/$(cache_path $EXEC_HASH)
test -f ${FILE_CAS}/$(cache_path $FILE_HASH)
test -f ${TREE_CAS}/$(cache_path $TREE_HASH)
"$JUST" install-cas --local-build-root "$LBRDIR" $ARGS ${EXEC_HASH} > /dev/null
"$JUST" install-cas --local-build-root "$LBRDIR" $ARGS ${FILE_HASH} > /dev/null
"$JUST" install-cas --local-build-root "$LBRDIR" $ARGS ${TREE_HASH} > /dev/null

# B) TEST UPLOAD
# --------------
echo "Check artifacts upload"

# Inject random string into source files
RANDOM_STRING=$(hostname).$(date +%s%N).$$
sed -i "s|RANDOM_STRING_1 \".*\"|RANDOM_STRING_1 \"$RANDOM_STRING\"|" greet/include/greet.hpp
sed -i "s|RANDOM_STRING_2 \".*\"|RANDOM_STRING_2 \"$RANDOM_STRING\"|" greet/src/greet.cpp

# Build greetlib locally
"$JUST_MR" --norc --just "$JUST" --local-build-root "$LBRDIR" --main main build main $ARGS

# Modify target cache origin
mv "$TCDIR/$LOCAL_EXECUTION_ID" "$TCDIR/$REMOTE_EXECUTION_ID"

# Check if greetlib successfully builds remotely
"$JUST_MR" --norc --just "$JUST" --local-build-root "$LBRDIR" --main main build main $ARGS $REMOTE_EXECUTION_ARGS ${AUTH_ARGS}

# Clean up test files
rm -rf "$TESTDIR" "$LBRDIR"
cd "$ROOT"

# ------------------------------------------------------------------------------
# Test Phase III: Test artifacts sync of 'provides' map
# ------------------------------------------------------------------------------

echo
echo "Test phase III"
echo

# Copy pydicts test files
cp -r "$ROOT/pydicts" "$TESTDIR"
cd "$TESTDIR"

# A) TEST DOWNLOAD
# ----------------
echo "Check artifacts download"

# Inject random string into source files
RANDOM_STRING=$(hostname).$(date +%s%N).$$
sed -i "s|\"foo\": \"[^\"]*\"|\"foo\": \"$RANDOM_STRING\"|" foo.py
sed -i "s|\"foo\": \"[^\"]*\"|\"foo\": \"$RANDOM_STRING\"|" bar.py

# Build pydicts remotely
"$JUST_MR" --norc --just "$JUST" --local-build-root "$LBRDIR" build json_from_py $ARGS $REMOTE_EXECUTION_ARGS ${AUTH_ARGS}

# 'exported_py' target contains a provides map,
#   which contains an abstract node (type 'convert'),
#     which contains value nodes,
#       which contain target results,
#         which contain KNOWN artifacts {foo,bar}.py

# Check if {foo,bar}.py have been downloaded correctly
if [ "${COMPATIBLE:-}" = "YES" ]; then
  readonly FOO_HASH=$(cat foo.py | sha256sum | cut -d' ' -f1)
  readonly BAR_HASH=$(cat bar.py | sha256sum | cut -d' ' -f1)
else
  readonly FOO_HASH=$(cat foo.py | git hash-object --stdin)
  readonly BAR_HASH=$(cat bar.py | git hash-object --stdin)
fi
test -f ${FILE_CAS}/$(cache_path $FOO_HASH)
test -f ${FILE_CAS}/$(cache_path $BAR_HASH)
"$JUST" install-cas --local-build-root "$LBRDIR" $ARGS ${FOO_HASH} > /dev/null
"$JUST" install-cas --local-build-root "$LBRDIR" $ARGS ${BAR_HASH} > /dev/null

# B) TEST UPLOAD
# --------------
echo "Check artifacts upload"

# Inject random string into source files
RANDOM_STRING=$(hostname).$(date +%s%N).$$
sed -i "s|\"foo\": \"[^\"]*\"|\"foo\": \"$RANDOM_STRING\"|" foo.py
sed -i "s|\"foo\": \"[^\"]*\"|\"foo\": \"$RANDOM_STRING\"|" bar.py

# Build pydicts locally
"$JUST_MR" --norc --just "$JUST" --local-build-root "$LBRDIR" build json_from_py $ARGS

# Modify target cache origin
mv "$TCDIR/$LOCAL_EXECUTION_ID" "$TCDIR/$REMOTE_EXECUTION_ID"

# Check if pydicts successfully builds remotely
"$JUST_MR" --norc --just "$JUST" --local-build-root "$LBRDIR" build json_from_py $ARGS $REMOTE_EXECUTION_ARGS ${AUTH_ARGS}

# Clean up test files
rm -rf "$TESTDIR" "$LBRDIR"
cd "$ROOT"
