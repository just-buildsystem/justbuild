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

JUST="$(pwd)/bin/tool-under-test"
LBR="${TEST_TMPDIR}/local-build-root"

mkdir src
cd src
touch ROOT
cat > RULES <<'EOF'
{ "provide":
  { "target_fields": ["deps"]
  , "expression":
    { "type": "RESULT"
    , "provides":
      { "type": "singleton_map"
      , "key": "foo"
      , "value":
        { "type": "disjoint_map_union"
        , "msg": "Dependencies may not overlap"
        , "$1":
          { "type": "foreach"
          , "range": {"type": "FIELD", "name": "deps"}
          , "body":
            {"type": "DEP_ARTIFACTS", "dep": {"type": "var", "name": "_"}}
          }
        }
      }
    }
  }
, "use provides":
  { "target_fields": ["deps"]
  , "expression":
    { "type": "RESULT"
    , "artifacts":
      { "type": "disjoint_map_union"
      , "msg": "Provided data may not overlap"
      , "$1":
        { "type": "foreach"
        , "range": {"type": "FIELD", "name": "deps"}
        , "body":
          { "type": "DEP_PROVIDES"
          , "dep": {"type": "var", "name": "_"}
          , "provider": "foo"
          }
        }
      }
    }
  }
}
EOF
cat > TARGETS <<'EOF'
{ "greeting.txt":
  { "type": "generic"
  , "arguments_config": ["NAME"]
  , "outs": ["greeting.txt"]
  , "cmds":
    [ { "type": "join"
      , "$1":
        [ { "type": "join_cmd"
          , "$1":
            [ "echo"
            , "Hello"
            , {"type": "var", "name": "NAME", "default": "unknown user"}
            ]
          }
        , " > greeting.txt"
        ]
      }
    ]
  }
, "world":
  { "type": "configure"
  , "target": "greeting.txt"
  , "config": {"type": "singleton_map", "key": "NAME", "value": "World"}
  }
, "another world":
  { "type": "configure"
  , "target": "greeting.txt"
  , "config": {"type": "singleton_map", "key": "NAME", "value": "World"}
  }
, "universe":
  { "type": "configure"
  , "target": "greeting.txt"
  , "config": {"type": "singleton_map", "key": "NAME", "value": "Universe"}
  }
, "unrelated":
  { "type": "generic"
  , "outs": ["foo.txt"]
  , "cmds": ["echo unrelated > foo.txt"]
  }
, "install-conflict":
  { "type": "install"
  , "dirs":
    [ ["world", "."]
    , ["unrelated", "."]
    , ["universe", "."]
    , ["another world", "."]
    ]
  }
, "rule-conflict":
  { "type": "provide"
  , "deps": ["world", "unrelated", "universe", "another world"]
  }
, "provided world": {"type": "provide", "deps": ["world"]}
, "another provided world": {"type": "provide", "deps": ["another world"]}
, "provided unrelated": {"type": "provide", "deps": ["unrelated"]}
, "provided universe": {"type": "provide", "deps": ["universe"]}
, "provided-conflict":
  { "type": "use provides"
  , "deps":
    [ "provided world"
    , "provided unrelated"
    , "provided universe"
    , "another provided world"
    ]
  }
, "rule-conflict-in-config":
  { "type": "provide"
  , "deps": ["world", "unrelated", "universe", "greeting.txt"]
  }
, "simple-rule-conflict-in-config":
  { "type": "provide"
  , "deps": ["unrelated", "universe", "greeting.txt"]
  }
}
EOF

"${JUST}" build --local-build-root "${LBR}" install-conflict \
          -f log --log-limit 0 2>&1 && exit 1 || :
echo
# We expect, in the error message, to see the full-qualified target names
# containing the conflicting artifacts, but not the target not containing
# this artifact.
grep '\["@","","","world"\]' log
grep '\["@","","","another world"\]' log
grep '\["@","","","universe"\]' log
grep '\["@","","","unrelated"\]' log && exit 1 || :
echo
echo


"${JUST}" build --local-build-root "${LBR}" rule-conflict \
          -f log --log-limit 0 2>&1 && exit 1 || :
echo
grep '\["@","","","world"\]' log
grep '\["@","","","another world"\]' log
grep '\["@","","","universe"\]' log
grep '\["@","","","unrelated"\]' log && exit 1 || :
echo
echo

"${JUST}" build --local-build-root "${LBR}" provided-conflict \
          -f log --log-limit 0 2>&1 && exit 1 || :
echo
grep '\["@","","","provided world"\]' log
grep '\["@","","","another provided world"\]' log
grep '\["@","","","provided universe"\]' log
grep '\["@","","","provided unrelated"\]' log && exit 1 || :

# Also verify that the non-null part of the effective configuration is
# reported.
"${JUST}" build --local-build-root "${LBR}" rule-conflict-in-config \
          -f log --log-limit 0 2>&1 -D '{"FOO": "bar", "NAME": "World"}' \
    && exit 1 || :
echo
grep -F '[["@","","","world"],{}]' log
grep -F '[["@","","","universe"],{}]' log
grep -F '[["@","","","greeting.txt"],{"NAME":"World"}]' log
grep -F '["@","","","unrelated"]' log && exit 1 || :
echo
echo

"${JUST}" build --local-build-root "${LBR}" simple-rule-conflict-in-config \
          -f log --log-limit 0 2>&1 -D '{"FOO": "bar", "NAME": null}' \
    && exit 1 || :
echo
grep -F '[["@","","","universe"],{}]' log
grep -F '[["@","","","greeting.txt"],{}]' log
grep -F '["@","","","unrelated"]' log && exit 1 || :
echo
echo

echo OK
