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

readonly ROOT="$(pwd)"
readonly LBRDIR="${TMPDIR}/local-build-root"
readonly JUST="${ROOT}/bin/tool-under-test"

touch ROOT
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "arguments_config": ["SHC"]
  , "cmds": ["first", "second"]
  , "sh -c": {"type": "var", "name": "SHC"}
  , "outs": ["some file"]
  }
}
EOF

"${JUST}" analyse --local-build-root "${LBRDIR}" \
  --dump-graph null.json 2>&1
[ $(jq -aM '.actions | .[] | .command == ["sh", "-c", "first\nsecond\n"]' null.json) = true ]

"${JUST}" analyse --local-build-root "${LBRDIR}" \
  -D '{"SHC": []}' --dump-graph empty.json 2>&1
[ $(jq -aM '.actions | .[] | .command == ["sh", "-c", "first\nsecond\n"]' empty.json) = true ]

"${JUST}" analyse --local-build-root "${LBRDIR}" \
  -D '{"SHC": ["custom-shell", "--fancy-option"]}' --dump-graph custom.json 2>&1
[ $(jq -aM '.actions | .[] | .command == ["custom-shell", "--fancy-option", "first\nsecond\n"]' custom.json) = true ]


echo OK
