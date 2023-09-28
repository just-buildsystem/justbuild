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

TOOL=$(realpath ./bin/tool-under-test)
mkdir -p .root
BUILDROOT=$(realpath .root)
mkdir -p out
OUTDIR=$(realpath out)

mkdir src
cd src
touch ROOT
cat > TARGETS <<'EOI'
{ "copy foo":
  { "type": "configure"
  , "arguments_config": ["FOO"]
  , "target": "use bar"
  , "config":
    { "type": "let*"
    , "bindings":
      [["BAR", {"type": "var", "name": "FOO"}], ["FOO", "REDACTED"]]
    , "body": {"type": "env", "vars": ["BAR", "FOO"]}
    }
  }
, "use bar":
  { "type": "file_gen"
  , "arguments_config": ["BAR"]
  , "data": {"type": "var", "name": "BAR", "default": "bar"}
  , "name": "out.txt"
  }
}
EOI


echo
${TOOL} analyse --local-build-root "${BUILDROOT}" \
        --dump-vars "${OUTDIR}/vars.json" \
        --dump-targets "${OUTDIR}/targets.json" \
        -D '{"FOO": "the value", "BAR": "unused!"}' 'copy foo' 2>&1
echo
cat "${OUTDIR}/vars.json"
echo
cat "${OUTDIR}/targets.json"
echo
copy_configs=$(cat "${OUTDIR}/targets.json" | jq -acM '."@" | ."" | ."" | ."copy foo"')
echo "${copy_configs}"
[ $(echo "${copy_configs}" | jq length) -eq 1 ]
config=$(echo "${copy_configs}" | jq '.[0]')
echo $config
[ $(echo "$config" | jq -acM 'keys') = '["FOO"]' ]
echo "$config" | jq -acM '."FOO"'
[ "$(echo "$config" | jq -acM '."FOO"')" = '"the value"' ]
[ "$(cat "${OUTDIR}/vars.json")" = '["FOO"]' ]

echo DONE
