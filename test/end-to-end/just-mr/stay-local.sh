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
readonly LBR2="${TEST_TMPDIR}/local-build-root-2"
readonly OUT="${TEST_TMPDIR}/out"
readonly OUT2="${TEST_TMPDIR}/out-2"
readonly LOG="${TEST_TMPDIR}/log"
readonly EMPTY="${TEST_TMPDIR}/empty-directory"
readonly SERVER="${PWD}/utils/null-server"
readonly SERVER_STATE="${TEST_TMPDIR}/server"

ARCHIVE_CONTENT=$(git hash-object src/data.tar)
echo "Archive has content $ARCHIVE_CONTENT"

mkdir -p "${SERVER_STATE}"
"${SERVER}" "${SERVER_STATE}/port" "${SERVER_STATE}/access" & server_pid=$!
trap "kill $server_pid" INT TERM EXIT
while [ '!' -f "${SERVER_STATE}/port" ]
do
    sleep 1s
done
# get port number as variable
port="$(cat "${SERVER_STATE}/port")"
echo "Server up and listening at port ${port}"

mkdir work
cd work
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "archive":
    { "repository":
      { "type": "archive"
      , "content": "${ARCHIVE_CONTENT}"
      , "fetch": "http://non-existent.example.org/data.tar"
      }
    }
  , "targets": {"repository": {"type": "file", "path": "targets"}}
  , "":
    { "repository": {"type": "distdir", "repositories": ["archive"]}
    , "target_root": "targets"
    }
  }
}
EOF
mkdir targets
cat > targets/TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["archive_id"]
  , "cmds": ["git hash-object data.tar > archive_id"]
  , "deps": ["data.tar"]
  }
}
EOF

echo
cat repos.json
echo
mkdir -p "${LOG}"
"${JUST_MR}" --norc --local-build-root "${LBR}" \
                    -r "127.0.0.1:${port}" \
                    --log-limit 5 -f "${LOG}/log" \
                    --distdir ../src \
                    setup  > conf.json
echo
cat "${LOG}/log"
echo
cat conf.json
echo
cat $(cat conf.json)
echo
# As a distdir (local directory!) was provided with all needed files,
# no attempt should be made to contact the remote-execution endpoint
echo
[ -f "${SERVER_STATE}/access" ] && cat "${SERVER_STATE}/access" && exit 1 || :

# The obtained configuraiton should be suitable for building, also remotely
"${JUST}" install -C "$(cat conf.json)" -o "${OUT}" \
          --local-build-root "${LBR}" \
          -r "${REMOTE_EXECUTION_ADDRESS}" 2>&1
echo
cat "${OUT}/archive_id"
[ $(cat "${OUT}/archive_id") = "${ARCHIVE_CONTENT}" ]
echo

# As the archive in question was input to an action, setup via the
# remote-execution endpoint should work now, even if the provided
# distdir is empty
mkdir -p "${EMPTY}"
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR2}" \
             -r "${REMOTE_EXECUTION_ADDRESS}" \
             --distdir ${EMPTY} \
             install -o "${OUT2}" 2>&1
cat "${OUT2}/archive_id"
[ $(cat "${OUT2}/archive_id") = "${ARCHIVE_CONTENT}" ]
echo

echo OK
