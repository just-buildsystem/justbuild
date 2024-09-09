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
readonly OUT="${TMPDIR}/out"
mkdir -p "${OUT}"

touch ROOT
cat > TARGETS <<EOF
{ "input-non-upwards":
  { "type": "generic"
  , "out_dirs": ["."]
  , "cmds": ["touch foo", "ln -s foo bar", "ln -s iNeXIstENt baz"]
  }
, "stage-non-upwards-links":
  { "type": "generic"
  , "out_dirs": ["."]
  , "cmds":
    [ "test -L bar || (printf \"\n\nwrong staging: bar must be a symlink\n\n\" && exit 1)"
    , "test -L baz || (printf \"\n\nwrong staging: baz must be a symlink\n\n\" && exit 1)"
    , "! test -e iNeXIstENt"
    ]
  , "deps": ["input-non-upwards"]
  }
, "input-non-contained":
  { "type": "generic"
  , "out_dirs": ["."]
  , "cmds": ["ln -s ../iNeXIstENt baz"]
  }
, "stage-non-contained-links":
  { "type": "generic"
  , "out_dirs": ["."]
  , "cmds": ["test -L baz || (printf \"\n\nwrong staging: baz must be a symlink\n\n\" && exit 1)"]
  , "deps": ["input-non-contained"]
  }
}
EOF

ARGS="--local-build-root=.root"
if [ "${COMPATIBLE:-}" = "YES" ]; then
  NAME="compatible"
  ARGS="$ARGS --compatible"
fi

REMOTE_EXECUTION_ARGS="-r ${REMOTE_EXECUTION_ADDRESS}"
if [ "${REMOTE_EXECUTION_PROPERTIES:-}" != "" ]; then
  REMOTE_EXECUTION_PROPS="$(printf " --remote-execution-property %s" ${REMOTE_EXECUTION_PROPERTIES})"
  REMOTE_EXECUTION_ARGS="${REMOTE_EXECUTION_ARGS} ${REMOTE_EXECUTION_PROPS}"
fi

FAILED=""

# Check that actions with non-upwards symlinks succeed
echo
echo "test input non-upwards locally"
${JUST} build -L '["env", "PATH='"${PATH}"'"]' ${ARGS} input-non-upwards 2>&1
${JUST} install -L '["env", "PATH='"${PATH}"'"]' ${ARGS} input-non-upwards \
                -o "${OUT}" 2>&1 && ls -alR "${OUT}" && rm -rf "${OUT}/*" || FAILED=YES

echo
echo "test input non-upwards remotely"
${JUST} build -L '["env", "PATH='"${PATH}"'"]' ${ARGS} ${REMOTE_EXECUTION_ARGS} input-non-upwards 2>&1
${JUST} install -L '["env", "PATH='"${PATH}"'"]' ${ARGS} ${REMOTE_EXECUTION_ARGS} input-non-upwards \
                -o "${OUT}" 2>&1 && ls -alR "${OUT}" && rm -rf "${OUT}/*" || FAILED=YES

echo
echo "test staging non-upwards locally"
${JUST} build -L '["env", "PATH='"${PATH}"'"]' ${ARGS} stage-non-upwards-links 2>&1 || FAILED=YES

echo
echo "test staging non-upwards remotely"
${JUST} build -L '["env", "PATH='"${PATH}"'"]' ${ARGS} ${REMOTE_EXECUTION_ARGS} stage-non-upwards-links 2>&1 || FAILED=YES

# Check that actions with non-contained upwards symlinks fail
echo
echo "test input non-contained locally"
${JUST} build -L '["env", "PATH='"${PATH}"'"]' ${ARGS} input-non-contained 2>&1 \
        && echo "this should have failed" && FAILED=YES
${JUST} install -L '["env", "PATH='"${PATH}"'"]' ${ARGS} input-non-contained -o "${OUT}" 2>&1 \
        && echo "this should have failed" && FAILED=YES \
        && ls -alR "${OUT}" && rm -rf "${OUT}/*" || echo "failed as expected"

echo
echo "test input non-contained remotely"
${JUST} build -L '["env", "PATH='"${PATH}"'"]' ${ARGS} ${REMOTE_EXECUTION_ARGS} input-non-contained 2>&1 \
        && echo "this should have failed" && FAILED=YES || echo "failed as expected"
${JUST} install -L '["env", "PATH='"${PATH}"'"]' ${ARGS} ${REMOTE_EXECUTION_ARGS} input-non-contained -o "${OUT}" 2>&1 \
        && echo "this should have failed" && FAILED=YES \
        && ls -alR "${OUT}" && rm -rf "${OUT}/*" || echo "failed as expected"

echo
echo "test staging non-contained locally"
${JUST} build -L '["env", "PATH='"${PATH}"'"]' ${ARGS} stage-non-contained-links 2>&1 \
        && echo "this should have failed" && FAILED=YES || echo "failed as expected"

echo
echo "test staging non-contained remotely"
${JUST} build -L '["env", "PATH='"${PATH}"'"]' ${ARGS} ${REMOTE_EXECUTION_ARGS} stage-non-contained-links 2>&1 \
        && echo "this should have failed" && FAILED=YES || echo "failed as expected"

if [ ! -z "${FAILED}" ]; then
  exit 1
fi

echo
echo OK
