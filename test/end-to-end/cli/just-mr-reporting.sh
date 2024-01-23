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

set -e

BUILDROOT="${TEST_TMPDIR}/build-root"
OUTDIR="${TEST_TMPDIR}/out"

mkdir tools
ln -s $(realpath ./bin/tool-under-test) tools/the-build-tool
ln -s $(realpath ./bin/mr-tool-under-test) tools/the-multi-repo-tool
export PATH=$(realpath tools):${PATH}

mkdir -p log
LOGDIR="$(realpath log)"

mkdir work
cd work
touch ROOT

cat > repos.json <<'EOF'
{ "repositories":
  { "":
    {"repository": {"type": "file", "path": ".", "pragma": {"to_git": true}}}
  }
}
EOF

cat > TARGETS <<'EOF'
{ "generated":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["echo Hello WoRlD > out.txt"]
  }
, "": {"type": "export", "target": "generated"}
}
EOF

the-multi-repo-tool --norc --just the-build-tool \
    --local-build-root "${BUILDROOT}" \
    --log-limit 1 -f "${LOGDIR}/warning.txt" \
    install -o "${OUTDIR}" 2>&1

# Verify that the build yields the expected result

grep WoRlD "${OUTDIR}/out.txt"

# Verify that the reported warning contains the actual
# tool names, not some hardcoded string

grep the-build-tool "${LOGDIR}/warning.txt"
grep the-multi-repo-tool "${LOGDIR}/warning.txt"
grep 'just build' "${LOGDIR}/warning.txt" && exit 1
grep 'just-mr' "${LOGDIR}/warning.txt" && exit 1


echo OK
