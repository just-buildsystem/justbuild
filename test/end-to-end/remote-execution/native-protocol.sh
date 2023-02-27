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

readonly DIRNAME="dir"
readonly OUT_DIRNAME="${DIRNAME}out"
readonly GITDIR="${TEST_TMPDIR}/git-root"
readonly LBRDIR="${TEST_TMPDIR}/local-build-root"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly JUST="${PWD}/bin/tool-under-test"
readonly RESULT="out.txt"
readonly CREDENTIALS_DIR="${PWD}/credentials"

echo
echo Create Git repository
echo
mkdir -p "${GITDIR}"
cd "${GITDIR}"
git init
git config user.name "Nobody"
git config user.email "nobody@example.org"
mkdir -p "${DIRNAME}"
echo foo > "${DIRNAME}/foo.txt"
echo bar > "${DIRNAME}/bar.txt"
git add "${DIRNAME}"
git commit -m "Initial commit"
readonly GIT_COMMIT_ID="$(git log -n 1 --format=%H)"
readonly GIT_TREE_ID="$(git log -n 1 --format=%T)"

cat > repos.json <<EOF
{ "repositories":
  { "test":
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "${GIT_COMMIT_ID}"
      , "repository": "${GITDIR}"
      }
    , "target_root": "test-targets-layer"
    }
  , "test-targets-layer": {"repository": {"type": "file", "path": "."}}
  }
}
EOF

cat > TARGETS <<EOF
{ "test":
  { "type": "generic"
  , "cmds": ["cp -r ${DIRNAME} ${OUT_DIRNAME}"]
  , "deps": [["TREE", null, "${DIRNAME}"]]
  , "out_dirs": ["${OUT_DIRNAME}"]
  }
}
EOF

readonly CONF="$("${JUST_MR}" -C repos.json --local-build-root="${LBRDIR}" setup)"

NAME="native"
ARGS=""
EQUAL="="
if [ "${COMPATIBLE:-}" = "YES" ]; then
  NAME="compatible"
  ARGS="--compatible"
  EQUAL="!="
fi

echo
echo Upload and download Git tree to local CAS in ${NAME} mode
echo
"${JUST}" build -C "${CONF}" --main test test --local-build-root="${LBRDIR}" --dump-artifacts "${RESULT}" ${ARGS} 2>&1
TREE_ID="$(jq -r ".${OUT_DIRNAME}.id" "${RESULT}" 2>&1)"
test ${TREE_ID} ${EQUAL} ${GIT_TREE_ID}

AUTH_ARGS=""
REMOTE_EXECUTION_ARGS="-r ${REMOTE_EXECUTION_ADDRESS}"
if [ "${REMOTE_EXECUTION_PROPERTIES:-}" != "" ]; then
  REMOTE_EXECUTION_ARGS="${REMOTE_EXECUTION_ARGS} --remote-execution-property ${REMOTE_EXECUTION_PROPERTIES}"
fi
if [ -f "${CREDENTIALS_DIR}/ca.crt" ]; then
  AUTH_ARGS=" --tls-ca-cert ${CREDENTIALS_DIR}/ca.crt "
  if [ -f "${CREDENTIALS_DIR}/client.crt" ]; then
    AUTH_ARGS=" --tls-client-cert ${CREDENTIALS_DIR}/client.crt "${AUTH_ARGS}
  fi
  if [ -f "${CREDENTIALS_DIR}/client.key" ]; then
    AUTH_ARGS=" --tls-client-key ${CREDENTIALS_DIR}/client.key "${AUTH_ARGS}
  fi
fi
echo
echo Upload and download Git tree to remote CAS in ${NAME} mode
echo
"${JUST}" build -C "${CONF}" --main test test ${REMOTE_EXECUTION_ARGS} ${AUTH_ARGS} --local-build-root="${LBRDIR}" --dump-artifacts "${RESULT}" ${ARGS} 2>&1
TREE_ID="$(jq -r ".${OUT_DIRNAME}.id" "${RESULT}" 2>&1)"
test ${TREE_ID} ${EQUAL} ${GIT_TREE_ID}
