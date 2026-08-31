#!/bin/sh
# Copyright 2026 The justbuild authors.
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
readonly LBRDIR="${PWD}/local-build-root"
readonly ESDIR="${PWD}/service-build-root"
readonly INFOFILE="${PWD}/info.json"
readonly PIDFILE="${PWD}/pid.txt"
readonly SERVER_LOG="${PWD}/server.log"
readonly CLIENT_LOG="${PWD}/client.log"

# The remote execution protocol only says that a server *may* announce the
# maximum total size of the blobs it accepts in a single batch request; servers
# that announce more than they are willing to deliver (or "0" == no limit) are
# valid within the specification, and answer oversized batch requests with
# INVALID_ARGUMENT. This test verifies that the client notices this discrepancy
# and falls back to lower batch sizes, instead of losing the blobs.

# Limit enforced by the server, way below the 3MiB the client will assume.
readonly MAX_BATCH_SIZE=65536
# Size of the blob to upload; above the limit to enforce blob streaming and
# reducing the limit some value >= MAX_BATCH_SIZE+1 (needed for download test).
readonly UPLOAD_SIZE=$((2 * ($MAX_BATCH_SIZE + 1)))
# Size of the blob to download; still above the new limit to enforce blob
# streaming for the download and a second limit reduction for this instance.
readonly DOWNLOAD_SIZE=$(($MAX_BATCH_SIZE + 1))

${JUST} execute --info-file "$INFOFILE" --pid-file "$PIDFILE" \
        --max-batch-size "${MAX_BATCH_SIZE}" \
        --max-batch-size-reported 0 \
        --local-launcher '["env", "PATH='"${PATH}"'"]' \
        --log-limit 5 -f "${SERVER_LOG}" --local-build-root ${ESDIR} 2>&1 &

for _ in `seq 1 10`
do
    if test -f "${INFOFILE}"
    then
        break
    fi
    sleep 1;
done

if ! test -f "${INFOFILE}"
then
    echo "Did not find ${INFOFILE}"
    exit 1
fi

readonly PORT=$(jq '."port"' "${INFOFILE}")

touch ROOT
dd if=/dev/zero of=upload.txt bs=1 count=${UPLOAD_SIZE}

cat <<EOF > TARGETS
{ "":
  { "type": "generic"
  , "cmds": ["dd if=upload.txt of=download.txt bs=1 count=${DOWNLOAD_SIZE}"]
  , "deps": ["upload.txt"]
  , "outs": ["download.txt"]
  }
}
EOF

echo "Build and stage, transferring blobs the server refuses to batch"
"${JUST}" install -r localhost:${PORT} --local-build-root="${LBRDIR}" \
          --log-limit 5 -f "${CLIENT_LOG}" -o . 2>&1

kill $(cat "${PIDFILE}")

echo "Verify the staged artifact is complete"
[ $(wc -c < download.txt) -eq ${DOWNLOAD_SIZE} ]
echo "SUCCESS"

echo "Verify the server rejected the oversized batch requests"
[ $(grep -c 'BatchUpdateBlobs: Attempted to write a total of' "${SERVER_LOG}") -eq 1 ]
[ $(grep -c 'BatchReadBlobs: Attempted to read a total of' "${SERVER_LOG}") -eq 1 ]
echo "SUCCESS"

echo "Verify the client lowered its limit and fell back sequential/streaming"
[ $(grep -c 'Reduced max batch transfer size' "${CLIENT_LOG}") -eq 2 ]
[ $(grep -c 'Falling back to sequential blob upload' "${CLIENT_LOG}") -eq 1 ]
[ $(grep -c 'Falling back to streaming API' "${CLIENT_LOG}") -eq 1 ]
echo "SUCCESS"
