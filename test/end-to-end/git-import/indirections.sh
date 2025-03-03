#!/bin/sh
# Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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


set -eu

readonly GIT_IMPORT="${PWD}/bin/git-import-under-test"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly REPO_DIRS="${TEST_TMPDIR}/repos"
readonly OUT="${TEST_TMPDIR}/out"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly WRKDIR="${PWD}"

mkdir -p "${REPO_DIRS}/foo"
cd "${REPO_DIRS}/foo"
cat > repos.json <<'EOF'
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "main"}
    , "bindings": {"rules": "rules"}
    }
  , "rules orig": {"repository": {"type": "file", "path": "rules"}}
  , "defaults": {"repository": {"type": "file", "path": "defaults"}}
  , "rules":
    { "repository": "rules orig"
    , "target_root": "defaults"
    , "rule_root": "rules"
    }
  }
}
EOF
mkdir main
cat > main/TARGETS <<'EOF'
{"": {"type": ["@", "rules", "", "normalize"], "file": ["foo.txt"]}}
EOF
echo foo > main/foo.txt
mkdir rules
cat > rules/RULES <<'EOF'
{ "defaults":
  { "string_fields": ["cmd"]
  , "expression":
    { "type": "RESULT"
    , "provides":
      { "type": "singleton_map"
      , "key": "cmd"
      , "value": {"type": "FIELD", "name": "cmd"}
      }
    }
  }
, "normalize":
  { "target_fields": ["file"]
  , "implicit": {"defaults": ["defaults"]}
  , "expression":
    { "type": "let*"
    , "bindings":
      [ [ "in"
        , { "type": "disjoint_map_union"
          , "$1":
            { "type": "++"
            , "$1":
              { "type": "foreach"
              , "range": {"type": "FIELD", "name": "file"}
              , "body":
                { "type": "foreach_map"
                , "range":
                  { "type": "DEP_ARTIFACTS"
                  , "dep": {"type": "var", "name": "_"}
                  }
                , "body":
                  { "type": "singleton_map"
                  , "key": "in"
                  , "value": {"type": "var", "name": "$_"}
                  }
                }
              }
            }
          }
        ]
      , [ "cmd"
        , { "type": "++"
          , "$1":
            { "type": "foreach"
            , "range": {"type": "FIELD", "name": "defaults"}
            , "body":
              { "type": "DEP_PROVIDES"
              , "dep": {"type": "var", "name": "_"}
              , "provider": "cmd"
              }
            }
          }
        ]
      , [ "sh cmd"
        , { "type": "join"
          , "$1":
            [ "cat in | "
            , {"type": "join_cmd", "$1": {"type": "var", "name": "cmd"}}
            , " > out"
            ]
          }
        ]
      , [ "out"
        , { "type": "ACTION"
          , "inputs": {"type": "var", "name": "in"}
          , "outs": ["out"]
          , "cmd": ["sh", "-c", {"type": "var", "name": "sh cmd"}]
          }
        ]
      ]
    , "body": {"type": "RESULT", "artifacts": {"type": "var", "name": "out"}}
    }
  }
}
EOF
mkdir defaults
cat > defaults/TARGETS <<'EOF'
{"defaults": {"type": "defaults", "cmd": ["tr", "a-z", "A-Z"]}}
EOF
git init 2>&1
git checkout --orphan foomaster 2>&1
git config user.name 'N.O.Body' 2>&1
git config user.email 'nobody@example.org' 2>&1
git add . 2>&1
git commit -m 'Add normalized foot' 2>&1


mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > TARGETS <<'EOF'
{"": {"type": "install", "files": {"normalized/foo": ["@", "foo", "", ""]}}}
EOF
cat > repos.template.json <<'EOF'
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo"}
    }
  }
}
EOF

echo
echo Import
echo

"${GIT_IMPORT}" -C repos.template.json \
                --as foo -b foomaster \
                "${REPO_DIRS}/foo" \
                > repos.json
echo
cat repos.json
echo


echo
echo Check: can build
echo
"${JUST_MR}" --norc --just "${JUST}" \
             -L '["env", "PATH='"${PATH}"'"]' \
             -C repos.json \
             --local-build-root "${LBR}" \
             install -o "${OUT}" 2>&1

grep FOO "${OUT}/normalized/foo"

echo OK
