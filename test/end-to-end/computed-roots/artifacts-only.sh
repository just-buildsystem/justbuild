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
readonly LBRDIR="${TMPDIR}/local-build-root"
readonly JUST="${ROOT}/bin/tool-under-test"
readonly OUT="${TMPDIR}/out"
readonly OUT2="${TMPDIR}/out2"

# The export target "" of the base root has
# - artifacts: TARGETS, artifacts.txt
# - runfiles: runfiles.txt
# So the glob in the TARGETS file of a computed root based
# on that export target depends on whether the runfiles are
# taken as part of the root (which is wrong) or not.
readonly BASE_ROOT="${ROOT}/base"
mkdir -p "${BASE_ROOT}"
cd "${BASE_ROOT}"
cat > TARGETS <<'EOF'
{ "": {"type": "export", "target": "root"}
, "root": {"type": "root", "targets": ["payload_targets"]}
}
EOF
cat > RULES <<'EOF'
{ "root":
  { "target_fields": ["targets"]
  , "expression":
    { "type": "RESULT"
    , "artifacts":
      { "type": "map_union"
      , "$1":
        [ { "type": "disjoint_map_union"
          , "$1":
            { "type": "foreach"
            , "range": {"type": "FIELD", "name": "targets"}
            , "body":
              { "type": "disjoint_map_union"
              , "$1":
                { "type": "foreach"
                , "range":
                  { "type": "values"
                  , "$1":
                    { "type": "DEP_ARTIFACTS"
                    , "dep": {"type": "var", "name": "_"}
                    }
                  }
                , "body":
                  { "type": "`"
                  , "$1":
                    { "TARGETS":
                      {"type": ",", "$1": {"type": "var", "name": "_"}}
                    }
                  }
                }
              }
            }
          }
        , { "type": "`"
          , "$1":
            { "artifact.txt":
              {"type": ",", "$1": {"type": "BLOB", "data": "ArTiFaCt"}}
            }
          }
        ]
      }
    , "runfiles":
      { "type": "`"
      , "$1":
        { "runfile.txt":
          {"type": ",", "$1": {"type": "BLOB", "data": "RUNFILE!"}}
        }
      }
    }
  }
}
EOF
cat > payload_targets <<'EOF'
{ "":
  { "type": "generic"
  , "deps": [["GLOB", null, "*.txt"]]
  , "outs": ["out"]
  , "cmds": ["cat $(ls *.txt |sort) > out"]
  }
}
EOF
git init 2>&1
git branch -m stable-1.0 2>&1
git config user.email "nobody@example.org" 2>&1
git config user.name "Nobody" 2>&1
git add . 2>&1
git commit -m "Initial commit" 2>&1
GIT_TREE=$(git log -n 1 --format="%T")

mkdir -p "${ROOT}/main"
cd "${ROOT}/main"
cat > repo-config.json <<EOF
{ "repositories":
  { "base":
    {"workspace_root": ["git tree", "${GIT_TREE}", "${BASE_ROOT}/.git"]}
  , "": {"workspace_root": ["computed", "base", "", "", {}]}
  }
}
EOF
cat repo-config.json


echo
echo Analyse base root to demonstrate the setup
echo
"${JUST}" analyse --local-build-root "${LBR}" -L '["env", "PATH='"${PATH}"'"]' \
   -C repo-config.json --main "base" 2>&1


echo
echo Build on the computed root
echo
"${JUST}" install --local-build-root "${LBR}" -L '["env", "PATH='"${PATH}"'"]' \
   -C repo-config.json -o "${OUT}" 2>&1
echo
cat "${OUT}/out"
echo
grep ArTiFaCt "${OUT}/out"
grep RUNFILE "${OUT}/out" && exit 1 || :


echo
echo Build on the computed root again, to verify same for a cached root
echo
"${JUST}" install --local-build-root "${LBR}" -L '["env", "PATH='"${PATH}"'"]' \
   -C repo-config.json -o "${OUT2}" 2>&1
echo
cat "${OUT2}/out"
echo
grep ArTiFaCt "${OUT2}/out"
grep RUNFILE "${OUT2}/out" && exit 1 || :




echo OK
