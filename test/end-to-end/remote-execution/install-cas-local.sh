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

readonly JUST="${PWD}/bin/tool-under-test"
readonly LBRDIR="${TEST_TMPDIR}/local-build-root"
readonly SETUP_LBRDIR="${TEST_TMPDIR}/build-root-for-setup-only"
readonly FILES_DIR="${TEST_TMPDIR}/data"
readonly OUT="${TEST_TMPDIR}/out"
mkdir -p "${FILES_DIR}"
mkdir -p "${OUT}"

LOCAL_ARGS=""
REMOTE_ARGS="${LOCAL_ARGS} -r ${REMOTE_EXECUTION_ADDRESS}"
if [ -n "${COMPATIBLE:-}" ]
then
    REMOTE_ARGS="${REMOTE_ARGS} --compatible"
    LOCAL_ARGS="${LOCAL_ARGS} --compatible"
fi

# Set up secanrio, by creating a hash that is only known remotely
# and one that is only known locally. Moreover, compute a hash of
# a file unknown to both.

readonly REMOTE_FILE="${FILES_DIR}/remote"
echo 'This file is only known to ReMoTe!!' > "${REMOTE_FILE}"
REMOTE_HASH=$("${JUST}" add-to-cas --local-build-root "${SETUP_LBRDIR}" ${REMOTE_ARGS} "${REMOTE_FILE}")
echo "Remote file has hash ${REMOTE_HASH}"

readonly LOCAL_FILE="${FILES_DIR}/local"
echo 'This file is only known LoCaLlY ...' > "${LOCAL_FILE}"
LOCAL_HASH=$("${JUST}" add-to-cas --local-build-root "${LBRDIR}" ${LOCAL_ARGS} "${LOCAL_FILE}")
echo "Local file has hash ${LOCAL_HASH}"

readonly UNKNOWN_FILE="${FILES_DIR}/unknown"
echo '... for Rumpelstiltskin is my name' > "${UNKNOWN_FILE}"
UNKNOWN_HASH=$("${JUST}" add-to-cas --local-build-root "${SETUP_LBRDIR}" ${LOCAL_ARGS} "${UNKNOWN_FILE}")
echo "Unkown file has hash ${UNKNOWN_HASH}"

# Verify that with the canonical arguments (as, e.g., just-mr might provide them
# with appropriate rc file) we can get both files correctly. Also verify that
# we get an exit code identifying an error if we failed to fetch the file.

echo remote, via stdout
"${JUST}" install-cas --local-build-root "${LBRDIR}" ${REMOTE_ARGS} \
          "${REMOTE_HASH}" > "${OUT}/remote"
cmp "${OUT}/remote" "${REMOTE_FILE}"
rm -f "${OUT}/remote"

echo remote, specified output file
"${JUST}" install-cas --local-build-root "${LBRDIR}" ${REMOTE_ARGS} \
          -o "${OUT}/remote" "${REMOTE_HASH}" 2>&1
cmp "${OUT}/remote" "${REMOTE_FILE}"

echo local, via stdout
"${JUST}" install-cas --local-build-root "${LBRDIR}" ${REMOTE_ARGS} \
          "${LOCAL_HASH}" > "${OUT}/local"
cmp "${OUT}/local" "${LOCAL_FILE}"
rm -f "${OUT}/local"

echo local, specified output file
"${JUST}" install-cas --local-build-root "${LBRDIR}" ${REMOTE_ARGS} \
          -o "${OUT}/local" "${LOCAL_HASH}" 2>&1
cmp "${OUT}/local" "${LOCAL_FILE}"

echo unknown, via stdout
"${JUST}" install-cas --local-build-root "${LBRDIR}" ${REMOTE_ARGS} \
          "${UNKNOWN_HASH}" 2>&1 && exit 1 || :

echo unknown, specified output path
"${JUST}" install-cas --local-build-root "${LBRDIR}" ${REMOTE_ARGS} \
          -o "${OUT}/unknown" "${UNKNOWN_HASH}" 2>&1 && exit 1 || :

echo OK
