#!/bin/sh
set -e

touch ROOT
cat > RULES <<'EOI'
{ "no-conflict":
  { "expression":
    { "type": "let*"
    , "bindings":
      [ [ "map"
        , { "type": "map_union"
          , "$1":
            [ { "type": "singleton_map"
              , "key": "foo.txt"
              , "value": {"type": "BLOB", "data": "Same String"}
              }
            , { "type": "singleton_map"
              , "key": "./foo.txt"
              , "value": {"type": "BLOB", "data": "Same String"}
              }
            ]
          }
        ]
      , [ "out"
        , { "type": "ACTION"
          , "inputs": {"type": "var", "name": "map"}
          , "outs": ["out"]
          , "cmd": ["cp", "foo.txt", "out"]
          }
        ]
      ]
    , "body": {"type": "RESULT", "artifacts": {"type": "var", "name": "out"}}
    }
  }
, "input-conflict":
  { "expression":
    { "type": "let*"
    , "bindings":
      [ [ "map"
        , { "type": "map_union"
          , "$1":
            [ { "type": "singleton_map"
              , "key": "foo.txt"
              , "value": {"type": "BLOB", "data": "Some content"}
              }
            , { "type": "singleton_map"
              , "key": "./foo.txt"
              , "value": {"type": "BLOB", "data": "A different content"}
              }
            ]
          }
        ]
      , [ "out"
        , { "type": "ACTION"
          , "inputs": {"type": "var", "name": "map"}
          , "outs": ["out"]
          , "cmd": ["cp", "foo.txt", "out"]
          }
        ]
      ]
    , "body": {"type": "RESULT", "artifacts": {"type": "var", "name": "out"}}
    }
  }
, "artifacts-conflict":
  { "expression":
    { "type": "RESULT"
    , "artifacts":
      { "type": "map_union"
      , "$1":
        [ { "type": "singleton_map"
          , "key": "foo.txt"
          , "value": {"type": "BLOB", "data": "Some content"}
          }
        , { "type": "singleton_map"
          , "key": "./foo.txt"
          , "value": {"type": "BLOB", "data": "A different content"}
          }
        ]
      }
    }
  }
, "runfiles-conflict":
  { "expression":
    { "type": "RESULT"
    , "runfiles":
      { "type": "map_union"
      , "$1":
        [ { "type": "singleton_map"
          , "key": "foo.txt"
          , "value": {"type": "BLOB", "data": "Some content"}
          }
        , { "type": "singleton_map"
          , "key": "./foo.txt"
          , "value": {"type": "BLOB", "data": "A different content"}
          }
        ]
      }
    }
  }
, "input-tree-conflict":
  { "expression":
    { "type": "let*"
    , "bindings":
      [ [ "map"
        , { "type": "map_union"
          , "$1":
            [ { "type": "singleton_map"
              , "key": "foo"
              , "value": {"type": "TREE"}
              }
            , { "type": "singleton_map"
              , "key": "./foo/bar.txt"
              , "value": {"type": "BLOB"}
              }
            ]
          }
        ]
      , [ "out"
        , { "type": "ACTION"
          , "inputs": {"type": "var", "name": "map"}
          , "outs": ["out"]
          , "cmd": ["cp", "foo.txt", "out"]
          }
        ]
      ]
    , "body": {"type": "RESULT", "artifacts": {"type": "var", "name": "out"}}
    }
  }
, "artifacts-tree-conflict":
  { "expression":
    { "type": "RESULT"
    , "artifacts":
      { "type": "map_union"
      , "$1":
        [ {"type": "singleton_map", "key": "foo", "value": {"type": "TREE"}}
        , { "type": "singleton_map"
          , "key": "./foo/bar.txt"
          , "value": {"type": "BLOB"}
          }
        ]
      }
    }
  }
, "runfiles-tree-conflict":
  { "expression":
    { "type": "RESULT"
    , "runfiles":
      { "type": "map_union"
      , "$1":
        [ {"type": "singleton_map", "key": "foo", "value": {"type": "TREE"}}
        , { "type": "singleton_map"
          , "key": "./foo/bar.txt"
          , "value": {"type": "BLOB"}
          }
        ]
      }
    }
  }
, "file-file-conflict":
  { "expression":
    { "type": "let*"
    , "bindings":
      [ [ "map"
        , { "type": "map_union"
          , "$1":
            [ { "type": "singleton_map"
              , "key": "foo/bar/baz.txt"
              , "value": {"type": "BLOB", "data": "Some content"}
              }
            , { "type": "singleton_map"
              , "key": "foo/bar/baz.txt/other/file.txt"
              , "value": {"type": "BLOB", "data": "A different content"}
              }
            ]
          }
        ]
      , [ "out"
        , { "type": "ACTION"
          , "inputs": {"type": "var", "name": "map"}
          , "outs": ["out"]
          , "cmd": ["cp", "foo.txt", "out"]
          }
        ]
      ]
    , "body": {"type": "RESULT", "artifacts": {"type": "var", "name": "out"}}
    }
  }
}
EOI
cat > TARGETS <<'EOI'
{ "no-conflict": {"type": "no-conflict"}
, "input-conflict": {"type": "input-conflict"}
, "artifacts-conflict": {"type": "artifacts-conflict"}
, "runfiles-conflict": {"type": "runfiles-conflict"}
, "input-tree-conflict": {"type": "input-tree-conflict"}
, "artifacts-tree-conflict": {"type": "artifacts-tree-conflict"}
, "runfiles-tree-conflict": {"type": "runfiles-tree-conflict"}
, "file-file-conflict": {"type": "file-file-conflict"}
}
EOI

echo
./bin/tool-under-test analyse no-conflict --dump-actions - 2>&1

echo
./bin/tool-under-test analyse -f log input-conflict 2>&1 && exit 1 || :
grep -i 'error.*input.*conflict.*foo\.txt' log

echo
./bin/tool-under-test analyse -f log artifacts-conflict 2>&1 && exit 1 || :
grep -i 'error.*artifacts.*conflict.*foo\.txt' log

echo
./bin/tool-under-test analyse -f log runfiles-conflict 2>&1 && exit 1 || :
grep -i 'error.*runfiles.*conflict.*foo\.txt' log

echo
./bin/tool-under-test analyse -f log input-tree-conflict 2>&1 && exit 1 || :
grep -i 'error.*input.*conflict.*tree.*foo' log

echo
./bin/tool-under-test analyse -f log artifacts-tree-conflict 2>&1 && exit 1 || :
grep -i 'error.*artifacts.*conflict.*tree.*foo' log

echo
./bin/tool-under-test analyse -f log runfiles-tree-conflict 2>&1 && exit 1 || :
grep -i 'error.*runfiles.*conflict.*tree.*foo' log

echo
./bin/tool-under-test analyse -f log file-file-conflict 2>&1 && exit 1 || :
grep -i 'error.*.*conflict.*foo/bar/baz.txt' log

echo
echo DONE
