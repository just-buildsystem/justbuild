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

readonly ROOT="$(pwd)"
readonly LBR="${TMPDIR}/local-build-root"
readonly OUT="${TMPDIR}/out"
readonly JUST="${ROOT}/bin/tool-under-test"

mkdir work
cd work
touch ROOT

cat > RULES <<'EOF'
{ "action":
  { "string_fields": ["cmd", "cwd", "outs"]
  , "target_fields": ["deps"]
  , "expression":
    { "type": "let*"
    , "bindings":
      [ [ "deps"
        , { "type": "map_union"
          , "$1":
            { "type": "foreach"
            , "range": {"type": "FIELD", "name": "deps"}
            , "body":
              {"type": "DEP_ARTIFACTS", "dep": {"type": "var", "name": "_"}}
            }
          }
        ]
      , [ "cwd"
        , { "type": "join"
          , "separator": "/"
          , "$1": {"type": "FIELD", "name": "cwd"}
          }
        ]
      , [ "action"
        , { "type": "ACTION"
          , "inputs": {"type": "var", "name": "deps"}
          , "outs": {"type": "FIELD", "name": "outs"}
          , "cmd": {"type": "FIELD", "name": "cmd"}
          , "cwd": {"type": "var", "name": "cwd"}
          }
        ]
      ]
    , "body":
      {"type": "RESULT", "artifacts": {"type": "var", "name": "action"}}
    }
  }
}
EOF
cat > TARGETS <<'EOF'
{ "inputs":
  { "type": "install"
  , "files":
    {"tools/bin/generate.sh": "generate.sh", "work/name.txt": "name.txt"}
  }
, "rule":
  { "type": "action"
  , "cmd": ["sh", "../tools/bin/generate.sh", "name.txt"]
  , "cwd": ["work"]
  , "outs": ["work/hello.txt", "log/log.txt"]
  , "deps": ["inputs"]
  }
, "generic":
  { "type": "generic"
  , "cmds": ["sh ../tools/bin/generate.sh name.txt"]
  , "cwd": "work"
  , "outs": ["work/hello.txt", "log/log.txt"]
  , "deps": ["inputs"]
  }
}
EOF
cat > generate.sh <<'EOF'
echo -n 'Hello ' > hello.txt
cat $1 >> hello.txt
echo '!' >> hello.txt
mkdir -p ../log
echo DoNe >> ../log/log.txt
EOF
echo -n WoRlD > name.txt

for target in rule generic
do

echo
echo Action obtained for target $target
echo
"${JUST}" analyse \
          --local-build-root "${LBR}" \
          --dump-actions - "$target" 2>&1

echo
echo Verify correct action execution for target $target
echo
"${JUST}" install \
          --local-build-root "${LBR}" \
          --local-launcher '["env", "PATH='"${PATH}"'"]' \
          -o "${OUT}/$target" "$target" 2>&1

grep WoRlD "${OUT}/$target/work/hello.txt"
grep DoNe "${OUT}/$target/log/log.txt"

done

echo OK
