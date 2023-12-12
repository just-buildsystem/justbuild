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

TOOL=$(realpath ./bin/tool-under-test)
BUILDROOT="${TEST_TMPDIR}/build-root"
OUT="${TEST_TMPDIR}/out"
mkdir -p "${OUT}"

mkdir src
cd src
touch ROOT
cat > RULES <<'EOF'
{ "provides":
  { "config_vars": ["VIA_CONFIG"]
  , "expression":
    { "type": "RESULT"
    , "provides":
      { "type": "map_union"
      , "$1":
        [ { "type": "singleton_map"
          , "key": "fixed_json"
          , "value": {"type": "singleton_map", "key": "foo", "value": "bar"}
          }
        , { "type": "singleton_map"
          , "key": "variable"
          , "value": {"type": "var", "name": "VIA_CONFIG"}
          }
        ]
      }
    }
  }
}
EOF
cat > TARGETS <<'EOF'
{"provides": {"type": "provides"}}
EOF


"${TOOL}" analyse --local-build-root "${BUILDROOT}" \
          --dump-provides "${OUT}/plain.json" 2>&1
cat "${OUT}/plain.json"
[ "$(jq '. == { "fixed_json": {"foo": "bar"}
              , "variable": null}' "${OUT}/plain.json")" = true ]

"${TOOL}" analyse --local-build-root "${BUILDROOT}" \
          -D '{"VIA_CONFIG": {"x": "y"}}' \
          --dump-provides "${OUT}/config.json" 2>&1
cat "${OUT}/config.json"
[ "$(jq '. == { "fixed_json": {"foo": "bar"}
              , "variable": {"x": "y"}}' "${OUT}/config.json")" = true ]

"${TOOL}" analyse --local-build-root "${BUILDROOT}" \
          -D '{"VIA_CONFIG": {"x": "y"}}' \
          --dump-provides - > "${OUT}/stdout.json"
diff "${OUT}/config.json" "${OUT}/stdout.json"

echo OK
