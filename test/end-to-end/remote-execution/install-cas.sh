#!/bin/sh
# Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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
readonly OUTDIR="${PWD}/out"

LOCAL_ARGS="--local-build-root ${LBRDIR}"
REMOTE_ARGS="${LOCAL_ARGS} -r ${REMOTE_EXECUTION_ADDRESS}"
if [ "${REMOTE_EXECUTION_PROPERTIES:-}" != "" ]
then
   REMOTE_ARGS="${REMOTE_ARGS} --remote-execution-property ${REMOTE_EXECUTION_PROPERTIES}"
fi
if [ -n "${COMPATIBLE:-}" ]
then
    REMOTE_ARGS="${REMOTE_ARGS} --compatible"
    LOCAL_ARGS="${LOCAL_ARGS} --compatible"
fi

# Build a tree remotely and get its identifier
mkdir src
cd src
touch ROOT
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "out_dirs": ["out"]
  , "cmds":
    [ "mkdir -p out/deep/inside/path"
    , "echo Hello World > out/deep/inside/path/hello.txt"
    ]
  }
}
EOF

"${JUST}" build ${REMOTE_ARGS} --dump-artifacts artifacts.json 2>&1
echo
cat artifacts.json
OUT="$(jq -r '.out.id' artifacts.json)::t"
echo $OUT
echo

# install to stdout in all possible ways
mkdir -p "${OUTDIR}/stdout"
"${JUST}" install-cas ${REMOTE_ARGS} --raw-tree "${OUT}" > "${OUTDIR}/stdout/remote-raw"
"${JUST}" install-cas ${REMOTE_ARGS} --remember "${OUT}" > "${OUTDIR}/stdout/remote"
"${JUST}" install-cas ${LOCAL_ARGS} "${OUT}" --raw-tree > "${OUTDIR}/stdout/local-raw"
"${JUST}" install-cas ${LOCAL_ARGS} "${OUT}" > "${OUTDIR}/stdout/local"

# verify consistency between local and remote values
cmp "${OUTDIR}/stdout/local-raw" "${OUTDIR}/stdout/remote-raw"
cmp "${OUTDIR}/stdout/local" "${OUTDIR}/stdout/remote"

echo OK
