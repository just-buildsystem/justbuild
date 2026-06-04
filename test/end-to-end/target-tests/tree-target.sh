#!/bin/sh
# Copyright 2026 Klaus Aehlig
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

# Verify that an analysed tree target has the empty map
# as provides information

mkdir -p  src
touch src/ROOT
cat > src/TARGETS <<'EOF'
{"": {"type": "configure", "target": ["TREE", null, "tree"]}}
EOF

mkdir -p src/tree/foo
echo data > src/tree/foo/bar

./bin/tool-under-test analyse --workspace-root src \
        -L '["env", "PATH='"${PATH}"'"]' \
        --local-build-root .root . '' --dump-provides provides.json 2>&1

cat provides.json
[ "$(jq '. == {}' provides.json)" = "true" ]

# Verify we can access provides information of a tree target

mkdir -p  src2
touch src2/ROOT
cat > src2/RULES <<'EOF'
{ "provides":
  { "target_fields": ["deps"]
  , "expression":
    { "type": "let*"
    , "bindings":
      [ [ "provides list"
        , { "type": "foreach"
          , "range": {"type": "FIELD", "name": "deps"}
          , "body":
            { "type": "DEP_PROVIDES"
            , "provider": "probably not present"
            , "dep": {"type": "var", "name": "_"}
            , "default": "default"
            }
          }
        ]
      , [ "provides list (json)"
        , { "type": "BLOB"
          , "data":
            { "type": "json_encode"
            , "$1": {"type": "var", "name": "provides list"}
            }
          }
        ]
      ]
    , "body":
      { "type": "RESULT"
      , "artifacts":
        { "type": "singleton_map"
        , "key": "provides-list.json"
        , "value": {"type": "var", "name": "provides list (json)"}
        }
      }
    }
  }
}
EOF
cat > src2/TARGETS <<'EOF'
{"": {"type": "provides", "deps": [["TREE", null, "tree"]]}}
EOF
mkdir -p src2/tree/foo
echo data > src2/tree/foo/bar

./bin/tool-under-test install -o . --workspace-root src2 \
        -L '["env", "PATH='"${PATH}"'"]' \
        --local-build-root .root . '' 2>&1

cat provides-list.json
[ "$(jq '. == ["default"]' provides-list.json)" = "true" ]

echo
echo OK
