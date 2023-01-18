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
readonly CREDENTIALS_DIR="${PWD}/credentials"

# create a sufficiently large (>4MB) file for testing upload/download (16MB)
dd if=/dev/zero of=large.file bs=1024 count=$((16*1024))

touch ROOT
cat > TARGETS <<EOF
{ "test":
  { "type": "generic"
  , "cmds": ["cp large.file out.file"]
  , "deps": [["FILE", null, "large.file"]]
  , "outs": ["out.file"]
  }
}
EOF

NAME="native"
COMMON_ARGS="--local-build-root=.root"
if [ "${COMPATIBLE:-}" = "YES" ]; then
  NAME="compatible"
  COMMON_ARGS="$COMMON_ARGS --compatible"
fi

run_tests() {
    local TYPE="local"
    local REMOTE_ARGS=""
    local REMOTE_BUILD_ARGS=""
    local AUTH_ARGS=""
    if [ -n "${1:-}" ] && [ -n "${2:-}" ]; then
        TYPE="remote"
        REMOTE_ARGS="-r $1"
        REMOTE_BUILD_ARGS="--remote-execution-property $2"
        if [ -f "${CREDENTIALS_DIR}/ca.crt" ]; then
          AUTH_ARGS=" --tls-ca-cert ${CREDENTIALS_DIR}/ca.crt "
          if [ -f "${CREDENTIALS_DIR}/client.crt" ]; then
            AUTH_ARGS=" --tls-client-cert ${CREDENTIALS_DIR}/client.crt "${AUTH_ARGS}
          fi
          if [ -f "${CREDENTIALS_DIR}/client.key" ]; then
            AUTH_ARGS=" --tls-client-key ${CREDENTIALS_DIR}/client.key "${AUTH_ARGS}
          fi
        fi
    fi
    ARGS="$COMMON_ARGS $REMOTE_ARGS ${AUTH_ARGS}"
    BUILD_ARGS="$ARGS $REMOTE_BUILD_ARGS"

    echo
    echo Upload and download to $NAME $TYPE CAS.
    echo
    "${JUST}" build test $BUILD_ARGS --dump-artifacts result
    echo SUCCESS

    local ARTIFACT_ID="$(cat result | jq -r '."out.file".id')"
    local ARTIFACT_SIZE="$(cat result | jq -r '."out.file".size')"

    echo
    echo Download from $NAME $TYPE CAS with full qualified object-id.
    echo
    rm -rf out.file
    "${JUST}" install-cas $ARGS -o out.file [$ARTIFACT_ID:$ARTIFACT_SIZE:f]
    cmp large.file out.file
    echo SUCCESS

    # omitting size is only supported in native mode or for local backend
    if [ "$NAME" = "native" ] || [ "$TYPE" = "local" ]; then
        echo
        echo Download from $NAME $TYPE CAS with missing size and file type.
        echo
        rm -rf out.file
        "${JUST}" install-cas $ARGS -o out.file $ARTIFACT_ID
        cmp large.file out.file
        echo SUCCESS
    fi
}

run_tests #local
run_tests "${REMOTE_EXECUTION_ADDRESS:-}" "${REMOTE_EXECUTION_PROPERTIES:-}"
