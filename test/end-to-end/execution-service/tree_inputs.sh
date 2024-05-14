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
readonly LBRDIR="${PWD}/local-build-root"

touch ROOT

# Check if we can collect and stage empty trees

cat > TARGETS <<'EOF'
{ "make_trees":
  { "type": "generic"
  , "cmds": ["mkdir -p foo bar/baz"]
  , "out_dirs": ["."]
  }
, "read_trees":
  { "type": "generic"
  , "deps": ["make_trees"]
  , "cmds": ["set -e", "ls -l foo", "ls -l bar/baz", "echo SUCCESS > result"]
  , "outs": ["result"]
  }
}
EOF

REMOTE_EXECUTION_ARGS="-r ${REMOTE_EXECUTION_ADDRESS}"
if [ "${REMOTE_EXECUTION_PROPERTIES:-}" != "" ]; then
  REMOTE_EXECUTION_PROPS="$(printf " --remote-execution-property %s" ${REMOTE_EXECUTION_PROPERTIES})"
  REMOTE_EXECUTION_ARGS="${REMOTE_EXECUTION_ARGS} ${REMOTE_EXECUTION_PROPS}"
fi
if [ -n "${COMPATIBLE:-}" ]; then
  REMOTE_EXECUTION_ARGS="${REMOTE_EXECUTION_ARGS} --compatible"
fi

"${JUST}" install ${REMOTE_EXECUTION_ARGS} --local-build-root="${LBRDIR}" -o . read_trees 2>&1

grep SUCCESS result

echo DONE
