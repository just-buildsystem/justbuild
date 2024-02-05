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

set -e

mkdir -p .root

mkdir -p  src
touch src/ROOT

# Check if we can collect and stage empty trees

cat > src/TARGETS <<'EOF'
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

./bin/tool-under-test install -o out --workspace-root src \
	-L '["env", "PATH='"${PATH}"'"]' \
	--local-build-root .root . read_trees 2>&1

grep SUCCESS out/result

echo DONE
