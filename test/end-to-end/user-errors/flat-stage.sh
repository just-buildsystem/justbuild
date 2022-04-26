#!/bin/sh
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
