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
            , { "type": "singleton_map"
              , "key": "bar/baz/../../not-upwards.txt"
              , "value": {"type": "BLOB", "data": "unrelated"}
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
, "upwards-inputs":
  { "expression":
    { "type": "let*"
    , "bindings":
      [ [ "map"
        , { "type": "singleton_map"
          , "key": "bar/../../foo.txt"
          , "value": {"type": "BLOB", "data": "Some content"}
          }
        ]
      , [ "out"
        , { "type": "ACTION"
          , "inputs": {"type": "var", "name": "map"}
          , "outs": ["out"]
          , "cmd": ["cp", "../foo.txt", "out"]
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
, "upwards-inputs": {"type": "upwards-inputs"}
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
./bin/tool-under-test analyse -f log upwards-inputs 2>&1 && exit 1 || :
grep -i 'error.*\.\./foo.txt' log

echo
echo DONE
