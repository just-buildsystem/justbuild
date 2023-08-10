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
set -e

ROOT=$(pwd)
JUST=$(realpath ./bin/tool-under-test)
BUILDROOT="${TEST_TMPDIR}/build-root"

touch ROOT
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "out_dirs": ["foo/bar"]
  , "cmds":
    [ "mkdir -p foo/bar/baz/greeting"
    , "echo Hello World > foo/bar/baz/greeting/hello.txt"
    ]
  }
}
EOF

"${JUST}" build --local-build-root "${BUILDROOT}" --dump-artifacts out.json 2>&1
echo
cat out.json
# foo/bar is the output artifact, and it is a tree
[ $(jq -rM '."foo/bar"."file_type"' out.json) = "t" ]

# Therefore, foo is not an output artifact, so requesting -P leaves stdout empty
"${JUST}" build --local-build-root "${BUILDROOT}" -P foo > foo.txt
[ -f foo.txt ] && [ -z "$(cat foo.txt)" ]

# Requesting foo/bar gives a human-readable description of the tree
"${JUST}" build --local-build-root "${BUILDROOT}" -P foo/bar  | grep baz

# going deepter into the tree we stil can get human-readable tree descriptions
"${JUST}" build --local-build-root "${BUILDROOT}" -P foo/bar/baz/greeting  | grep hello.txt

# Files inside the tree can be retrieved
"${JUST}" build --local-build-root "${BUILDROOT}" -P foo/bar/baz/greeting/hello.txt  | grep  World

echo OK
