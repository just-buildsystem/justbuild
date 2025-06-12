#!/bin/sh
# Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly JUST_ARGS="--local-build-root ${LBR}"

readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR_MR="${TEST_TMPDIR}/local-build-root-mr"
readonly JUST_MR_ARGS="--norc --local-build-root ${LBR_MR} --just ${JUST}"

readonly OUT="${TEST_TMPDIR}/out"
mkdir -p "${OUT}"

STORAGE="${LBR}/protocol-dependent/generation-0"
COMPATIBLE_ARGS=""
if [ -n "${COMPATIBLE:-}" ]; then
  COMPATIBLE_ARGS="--compatible"
fi

mkdir work && cd work

touch ROOT
cat > repos.json <<'EOF'
{ "repositories":
  { "":
    {"repository": {"type": "file", "path": ".", "pragma": {"to_git": true}}}
  }
}
EOF

cat > TARGETS <<'EOF'
{ "file":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["echo foo > out.txt"]
  }
, "": {"type": "install", "dirs": [["file", "out"]]}
}
EOF

cat TARGETS

# Build to fill the cache
"${JUST_MR}" ${JUST_MR_ARGS} build ${JUST_ARGS} ${COMPATIBLE_ARGS} \
          -L '["env", "PATH='"${PATH}"'"]' 2>&1

if [ ! -d ${STORAGE} ]; then
    echo "Storage has not been created"
    exit 1
fi;

# Run regular gc with --no-rotate to ensure compactification gets triggered:
"${JUST_MR}" ${JUST_MR_ARGS} gc ${JUST_ARGS} --no-rotate --log-limit 4 \
  -f "${OUT}/gc" 2>&1

grep 'Compactification has been started' "${OUT}/gc"

# Run gc with --all to ensure compactification doesn't get triggered:
"${JUST_MR}" ${JUST_MR_ARGS} gc ${JUST_ARGS} --all --log-limit 4 \
  -f "${OUT}/gc2" 2>&1

grep 'Compactification has been started' "${OUT}/gc2" && exit 1

if [ -d ${STORAGE} ]; then
    echo "Storage has not been deleted"
    exit 1
fi;

echo OK
