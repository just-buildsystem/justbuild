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

touch ROOT
cat > TARGETS <<EOF
{ "input":
  { "type": "generic"
  , "out_dirs": ["."]
  , "cmds": ["touch foo", "ln -s foo bar", "ln -s iNeXIstENt baz"]
  }
, "stage-links":
  { "type": "generic"
  , "out_dirs": ["."]
  , "cmds":
    [ "test -L bar || (printf \"\n\nwrong staging: bar must be a symlink\n\n\" && exit 1)"
    , "test -L baz || (printf \"\n\nwrong staging: baz must be a symlink\n\n\" && exit 1)"
    , "! test -e iNeXIstENt"
    ]
  , "deps": ["input"]
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
  REMOTE_EXECUTION_ARGS="${REMOTE_EXECUTION_ARGS} --remote-execution-property ${REMOTE_EXECUTION_PROPERTIES}"
fi

echo "test staging locally"
${JUST} build -L '["env", "PATH='"${PATH}"'"]' ${ARGS} stage-links

echo "test staging remotely"
${JUST} build -L '["env", "PATH='"${PATH}"'"]' ${ARGS} ${REMOTE_EXECUTION_ARGS} stage-links
