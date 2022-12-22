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

mkdir tool-root
mkdir work
cd work
touch ROOT
cat > RULES <<'EOI'
{ "2^3":
  { "expression":
    { "type": "let*"
    , "bindings":
      [ ["blob", {"type": "BLOB", "data": "Hello World"}]
      , [ "two"
        , { "type": "TREE"
          , "$1":
            { "type": "map_union"
            , "$1":
              [ { "type": "singleton_map"
                , "key": "0"
                , "value": {"type": "var", "name": "blob"}
                }
              , { "type": "singleton_map"
                , "key": "1"
                , "value": {"type": "var", "name": "blob"}
                }
              ]
            }
          }
        ]
      , [ "four"
        , { "type": "TREE"
          , "$1":
            { "type": "map_union"
            , "$1":
              [ { "type": "singleton_map"
                , "key": "0"
                , "value": {"type": "var", "name": "two"}
                }
              , { "type": "singleton_map"
                , "key": "1"
                , "value": {"type": "var", "name": "two"}
                }
              ]
            }
          }
        ]
      , [ "eight"
        , { "type": "TREE"
          , "$1":
            { "type": "map_union"
            , "$1":
              [ { "type": "singleton_map"
                , "key": "0"
                , "value": {"type": "var", "name": "four"}
                }
              , { "type": "singleton_map"
                , "key": "1"
                , "value": {"type": "var", "name": "four"}
                }
              ]
            }
          }
        ]
      ]
    , "body":
      { "type": "RESULT"
      , "artifacts":
        { "type": "singleton_map"
        , "key": "data"
        , "value": {"type": "var", "name": "eight"}
        }
      }
    }
  }
}
EOI
cat > TARGETS <<'EOI'
{ "data": {"type": "2^3"}
, "ALL":
  { "type": "generic"
  , "outs": ["index.txt"]
  , "cmds": ["find data -type f | sort > index.txt"]
  , "deps": ["data"]
  }
}
EOI

echo
echo Analysis
echo
../bin/tool-under-test analyse data \
  --local-build-root ../tool-root --dump-trees ../trees.json --dump-blobs ../blobs.json 2>&1
echo
echo Blobs
echo
cat ../blobs.json
[ $(cat ../blobs.json | jq -acM 'length') -eq 1 ]
echo
echo Trees
echo
cat ../trees.json
[ $(cat ../trees.json | jq -acM 'length') -eq 3 ]
echo
echo Build
echo
../bin/tool-under-test install -o ../out --local-build-root ../tool-root 2>&1
echo
echo Index
echo
cat ../out/index.txt
echo
[ $(cat ../out/index.txt | wc -l) -eq 8 ]
