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
readonly GITDIR="${TEST_TMPDIR}/src"
readonly LBRDIR="${TEST_TMPDIR}/local-build-root"

mkdir -p ${GITDIR}
cd ${GITDIR}
git init
git config user.name "Nobody"
git config user.email "nobody@example.org"
mkdir -p foo/bar/baz
echo `hostname`.`date +%s`.$$ > foo/bar/baz/data.txt
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["find . > out.txt"]
  , "deps": [["TREE", null, "."]]
  }
}
EOF
git add .
git commit -m 'Generated new data'
readonly TREE=$(git log --pretty=%T)

cd ${TEST_TMPDIR}
cat > repos.json <<EOF
{ "repositories":
  {"": {"workspace_root": ["git tree", "${TREE}", "${GITDIR}"]}}
}
EOF

ARGS=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  ARGS="--compatible"
fi

# Build locally
export CONF="$(realpath repos.json)"
"${JUST}" build -C "${CONF}" --local-build-root="${LBRDIR}" ${ARGS} 2>&1

# Build remotely
if [ "${REMOTE_EXECUTION_ADDRESS:-}" != "" ]; then
  REMOTE_EXECUTION_ARGS="-r ${REMOTE_EXECUTION_ADDRESS}"
  if [ "${REMOTE_EXECUTION_PROPERTIES:-}" != "" ]; then
    REMOTE_EXECUTION_ARGS="${REMOTE_EXECUTION_ARGS} --remote-execution-property ${REMOTE_EXECUTION_PROPERTIES}"
  fi
  "${JUST}" build -C "${CONF}" --local-build-root="${LBRDIR}" ${ARGS} ${REMOTE_EXECUTION_ARGS} 2>&1
fi
