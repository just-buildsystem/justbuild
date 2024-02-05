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

TOOL=$(realpath ./bin/tool-under-test)
mkdir -p .root
BUILDROOT=$(realpath .root)
mkdir -p out
OUTDIR_BASE=$(realpath out)

echo {} > repos.json
export CONF=$(realpath repos.json)

mkdir src
cd src
touch ROOT

echo -n A > a.txt
chmod 644 a.txt
echo -n B > b.txt
echo -n C > c.txt
chmod 755 b.txt
mkdir d.txt

mkdir foo
echo too deep > foo/a.txt

cat > RULES <<'EOI'
{ "reflect":
  { "target_fields": ["deps"]
  , "expression":
    { "type": "let*"
    , "bindings":
      [ [ "inputs"
        , { "type": "map_union"
          , "$1":
            { "type": "foreach"
            , "var": "x"
            , "range": {"type": "FIELD", "name": "deps"}
            , "body":
              {"type": "DEP_ARTIFACTS", "dep": {"type": "var", "name": "x"}}
            }
          }
        ]
      , [ "keys"
        , { "type": "singleton_map"
          , "key": "keys.txt"
          , "value":
            { "type": "BLOB"
            , "data":
              { "type": "join"
              , "separator": " "
              , "$1": {"type": "keys", "$1": {"type": "var", "name": "inputs"}}
              }
            }
          }
        ]
      , [ "content"
        , { "type": "ACTION"
          , "inputs":
            { "type": "to_subdir"
            , "subdir": "work"
            , "$1": {"type": "var", "name": "inputs"}
            }
          , "outs": ["content.txt"]
          , "cmd":
            [ "sh"
            , "-c"
            , { "type": "join"
              , "separator": " "
              , "$1":
                [ "mkdir -p work && cd work &&"
                , { "type": "join_cmd"
                  , "$1":
                    { "type": "++"
                    , "$1":
                      [ ["cat"]
                      , { "type": "keys"
                        , "$1": {"type": "var", "name": "inputs"}
                        }
                      ]
                    }
                  }
                , "> ../content.txt"
                ]
              }
            ]
          }
        ]
      , [ "outputs"
        , { "type": "map_union"
          , "$1":
            [ {"type": "var", "name": "keys"}
            , {"type": "var", "name": "content"}
            ]
          }
        ]
      ]
    , "body":
      {"type": "RESULT", "artifacts": {"type": "var", "name": "outputs"}}
    }
  }
}
EOI

cat > TARGETS <<'EOI'
{ "b.txt": {"type": "file_gen", "name": "b-new.txt", "data": "fromtarget"}
, "enumeration": {"type": "reflect", "deps": ["a.txt", "b.txt", "c.txt"]}
, "glob": {"type": "reflect", "deps": [["GLOB", null, "*.txt"]]}
, "with_target":
  {"type": "reflect", "deps": [["GLOB", null, "*.txt"], "b.txt"]}
, "not_top_level": {"type": "reflect", "deps": [["GLOB", null, "foo/*.txt"]]}
}
EOI

do_test() {
  echo === Enumeration refres to targets ===

  ${TOOL} install -L '["env", "PATH='"${PATH}"'"]' -C ${CONF} --local-build-root ${BUILDROOT} -o ${OUTDIR}/enum enumeration 2>&1

  cat ${OUTDIR}/enum/keys.txt
  echo
  cat ${OUTDIR}/enum/content.txt
  echo

  [ "$(cat ${OUTDIR}/enum/keys.txt)" = "a.txt b-new.txt c.txt" ]
  [ "$(cat ${OUTDIR}/enum/content.txt)" = "AfromtargetC" ]

  echo === Glob always refres to files and directories are ignored ===

  ${TOOL} install -L '["env", "PATH='"${PATH}"'"]' -C ${CONF} --local-build-root ${BUILDROOT} -o ${OUTDIR}/glob glob 2>&1

  cat ${OUTDIR}/glob/keys.txt
  echo
  cat ${OUTDIR}/glob/content.txt
  echo

  [ "$(cat ${OUTDIR}/glob/keys.txt)" = "a.txt b.txt c.txt" ]
  [ "$(cat ${OUTDIR}/glob/content.txt)" = "ABC" ]

  echo === Globs and targets can be combined ===

  ${TOOL} install -L '["env", "PATH='"${PATH}"'"]' -C ${CONF} --local-build-root ${BUILDROOT} -o ${OUTDIR}/with_target with_target 2>&1

  cat ${OUTDIR}/with_target/keys.txt
  echo
  cat ${OUTDIR}/with_target/content.txt
  echo

  [ "$(cat ${OUTDIR}/with_target/keys.txt)" = "a.txt b-new.txt b.txt c.txt" ]
  [ "$(cat ${OUTDIR}/with_target/content.txt)" = "AfromtargetBC" ]

  echo === Globs only inspect the top-level directory of the module ===

  ${TOOL} install -L '["env", "PATH='"${PATH}"'"]' -C ${CONF} --local-build-root ${BUILDROOT} -o ${OUTDIR}/not_top_level not_top_level 2>&1

  cat ${OUTDIR}/not_top_level/keys.txt
  echo
  cat ${OUTDIR}/not_top_level/content.txt
  echo

  [ -z "$(cat ${OUTDIR}/not_top_level/keys.txt)" ]
  [ -z "$(cat ${OUTDIR}/not_top_level/content.txt)" ]

  echo === Done ===
}


echo
echo '***** Tests on the plain file system *****'
echo
OUTDIR=${OUTDIR_BASE}/file_system
do_test

echo
echo '***** Tests on a git root *****'
echo

git init
git add .
git config user.name "N.O.Body"
git config user.email "nobody@example.com"
git commit -m 'Initial commit'
REPO=$(realpath .git)
TREE=$(git log -n1 --format='%T')

cat > $CONF <<EOF
{"repositories": {"": {"workspace_root": ["git tree", "${TREE}", "${REPO}"]}}}
EOF
echo
cat $CONF
echo

OUTDIR=${OUTDIR_BASE}/git
do_test
