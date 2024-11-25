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
readonly LBRDIR="$TMPDIR/local-build-root"
readonly JUST="${ROOT}/bin/tool-under-test"
readonly JUST_MR="${ROOT}/bin/mr-tool-under-test"
readonly OUT="${ROOT}/out"

readonly BASE_ROOT="${ROOT}/base"
mkdir -p "${BASE_ROOT}"
cd "${BASE_ROOT}"
cat > generate.py <<'EOF'
import json
import sys

COUNT = int(sys.argv[1])
targets = {}
for i in range(COUNT):
  targets["%d" % i] = {"type": "generic", "outs": ["%d.txt" %i],
                       "cmds": ["seq 0 %d > %d.txt" % (i, i)]}
targets[""] = {"type": "generic",
               "deps": ["%d" % i for i in range(COUNT)],
               "cmds": [" ".join(["cat"] + ["%d.txt" % i for i in range(COUNT)]
                                 + ["> out"])],
               "outs": ["out"]}
print (json.dumps(targets, indent=2))
EOF
cat > TARGETS <<'EOF'
{ "": {"type": "export", "flexible_config": ["COUNT"], "target": "generate"}
, "generate":
  { "type": "generic"
  , "arguments_config": ["COUNT"]
  , "outs": ["TARGETS"]
  , "deps": ["generate.py"]
  , "cmds":
    [ { "type": "join"
      , "separator": " "
      , "$1":
        [ "python3"
        , "generate.py"
        , {"type": "var", "name": "COUNT"}
        , ">"
        , "TARGETS"
        ]
      }
    ]
  }
}
EOF

mkdir -p "${ROOT}/main"
cd "${ROOT}/main"
touch ROOT

cat > repo-config.json <<EOF
{ "repositories":
    { "base":
        { "repository":
            { "type": "file"
            , "path": "${BASE_ROOT}"
            , "pragma": {"to_git": true}
            }
        }
    , "derived":
        { "repository":
            { "type": "computed"
            , "repo": "base"
            , "target": ["", ""]
            , "config": {"COUNT": "10"}
            }
        }
    , "other derived":
        { "repository":
            { "type": "computed"
            , "repo": "base"
            , "target": ["", ""]
            , "config": {"COUNT": "12"}
            }
        }
    }
}
EOF
cat repo-config.json

echo
echo Building base, for reference
echo
"${JUST_MR}" --norc --local-build-root "${LBRDIR}" -C repo-config.json \
            --main base  --just "${JUST}" \
            install -D '{"COUNT": "10"}' \
            -L '["env", "PATH='"${PATH}"'"]' -o "${OUT}/base" 2>&1
echo
cat "${OUT}/base/TARGETS"

echo
echo Building computed
echo
"${JUST_MR}" --norc --local-build-root "${LBRDIR}" -C repo-config.json \
            --main derived --just "${JUST}" \
            install -L '["env", "PATH='"${PATH}"'"]'  -o "${OUT}/derived" 2>&1
echo

[ "$(cat "${OUT}/derived/out" | wc -l)" -eq 55 ]

echo
echo Building a different computed root, without reference build
echo
"${JUST_MR}" --norc --local-build-root "${LBRDIR}" -C repo-config.json \
            --main 'other derived' --just "${JUST}" \
            install -L '["env", "PATH='"${PATH}"'"]' \
            -o "${OUT}/other-derived" 2>&1
echo

[ "$(cat "${OUT}/other-derived/out" | wc -l)" -eq 78 ]

echo OK
