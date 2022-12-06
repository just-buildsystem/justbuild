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

touch ROOT

cat <<'EOF' > TARGETS
{ "":
  { "type": "file_gen"
  , "name": "read-runfiles"
  , "data":
    { "type": "join"
    , "separator": ";"
    , "$1": {"type": "runfiles", "dep": "uses config"}
    }
  , "deps": ["uses config"]
  }
, "uses config":
  { "type": "file_gen"
  , "arguments_config": ["NAME"]
  , "name": {"type": "var", "name": "NAME", "default": "null"}
  , "data": "irrelevant"
  }
}
EOF

bin/tool-under-test analyse -D '{"NAME": "here-be-dragons"}' \
    --dump-targets targets.json --dump-blobs blobs.json 2>&1
echo
echo "Blobs"
cat blobs.json
[ $(jq '. == ["here-be-dragons"]' blobs.json) = "true" ]
echo

echo "Targets"
cat targets.json
[ $(jq '."@"."".""."" == [{"NAME": "here-be-dragons"}]' targets.json) = "true" ]

echo OK
