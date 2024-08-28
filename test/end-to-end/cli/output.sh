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

ROOT="$(pwd)"
JUST="${ROOT}/bin/tool-under-test"
JUST_MR="${ROOT}/bin/mr-tool-under-test"
LBR="${TEST_TMPDIR}/build-root"
LOG="${ROOT}/log"

mkdir work
cd work
touch ROOT

cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": "."}}}}
EOF

cat > TARGETS <<'EOF'
{ "generic":
  { "type": "generic"
  , "outs": ["hello.txt"]
  , "cmds": ["echo Hello World > hello.txt"]
  }
, "": {"type": "install", "files": {"generic": "generic"}}
}
EOF

${JUST_MR} --norc -L '["env", "PATH='"${PATH}"'"]' --local-build-root "${LBR}" \
           --just "${JUST}" \
           -f "${LOG}" build 2>&1
echo

# Sanity check on verbosity of output

## By default, we should be at a log level reporting resulting artifacts
HELLO_BLOB_ID=$(echo Hello World | git hash-object --stdin)
grep -i $HELLO_BLOB_ID "${LOG}"

## This build should not cause any errors or warnings
grep -i error "${LOG}" && exit 1 || :
grep -i warn "${LOG}" && exit 1 || :

## Such a simple build should not have an overly long log
[ $(cat "${LOG}" | wc -l) -lt 100 ]

echo OK
