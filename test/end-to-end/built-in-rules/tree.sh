#!/bin/bash
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

readonly LBRDIR="$TMPDIR/local-build-root"

touch ROOT
touch a.txt

cat <<'EOF' > TARGETS
{ "": {"type": "tree", "name": "where/to/stage", "deps": ["content"]}
, "content":
  {"type": "install", "files": {"a": "a.txt", "b": "a.txt", "c/d": "a.txt"}}
}
EOF

bin/tool-under-test analyse \
    --local-build-root "$LBRDIR" --dump-trees trees.json --dump-artifacts-to-build artifacts.json 2>&1
echo
echo Artifacts
cat artifacts.json
[ $(jq 'keys == ["where/to/stage"]' artifacts.json) = "true" ]
tree_id=$(jq '."where/to/stage"."data"."id"' artifacts.json)

echo
cat trees.json
[ $(jq 'keys == ['$tree_id']' trees.json) = "true" ]
[ $(jq '.'$tree_id' | keys == ["a", "b", "c/d"]' trees.json) = "true" ]

echo OK
