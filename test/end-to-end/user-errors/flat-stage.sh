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

mkdir .tool-root
touch ROOT
mkdir subdir
touch foo.txt subdir/foo.txt
cat > RULES <<'EOI'
{ "data":
  { "target_fields": ["srcs"]
  , "string_fields": ["flat"]
  , "expression":
    { "type": "let*"
    , "bindings":
      [ [ "srcs"
        , { "type": "map_union"
          , "$1":
            { "type": "foreach"
            , "var": "x"
            , "range": {"type": "FIELD", "name": "srcs"}
            , "body":
              {"type": "DEP_ARTIFACTS", "dep": {"type": "var", "name": "x"}}
            }
          }
        ]
      , ["internal information", "DeBuG-InFoRmAtIoN"]
      , [ "result"
        , { "type": "to_subdir"
          , "subdir": "data"
          , "flat": {"type": "FIELD", "name": "flat"}
          , "msg":
            [ "DataRuleSpecificErrorMessage"
            , {"type": "var", "name": "internal information"}
            ]
          , "$1": {"type": "var", "name": "srcs"}
          }
        ]
      ]
    , "body":
      {"type": "RESULT", "artifacts": {"type": "var", "name": "result"}}
    }
  }
}
EOI

cat > TARGETS <<'EOI'
{ "full": {"type": "data", "srcs": ["foo.txt", "subdir/foo.txt"]}
, "flat":
  {"type": "data", "srcs": ["foo.txt", "subdir/foo.txt"], "flat": ["YES"]}
}
EOI


bin/tool-under-test build --local-build-root .tool-root -f build.log full 2>&1
echo
grep 'DataRuleSpecificErrorMessage' build.log && exit 1 || :
grep 'DeBuG-InFoRmAtIoN' build.log && exit 1 || :

bin/tool-under-test build --local-build-root .tool-root -f build.log flat 2>&1 && exit 1 || :
echo
grep 'DataRuleSpecificErrorMessage' build.log
grep "DeBuG-InFoRmAtIoN" build.log

echo OK
