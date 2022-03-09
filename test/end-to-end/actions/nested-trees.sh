#!/bin/sh
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
  --dump_trees ../trees.json --dump_blobs ../blobs.json 2>&1
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
../bin/tool-under-test install -o ../out  --local_build_root ../tool-root 2>&1
echo
echo Index
echo
cat ../out/index.txt
echo
[ $(cat ../out/index.txt | wc -l) -eq 8 ]
