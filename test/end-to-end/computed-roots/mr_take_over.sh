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

readonly SRC_DIR="${ROOT}/src"
mkdir -p "${SRC_DIR}"
cat > "${SRC_DIR}/generate.py" <<'EOF'
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

readonly TARGETS_DIR="${ROOT}/targets"
mkdir -p "${TARGETS_DIR}"
cd "${TARGETS_DIR}"
cat > "TARGETS.collect" << EOF
{ "": {"type": "export", "target": "collect"}
, "collect":
    { "type": "install"
    , "deps": [["TREE", null, "."]]
    }
}
EOF

cat > TARGETS.compute <<'EOF'
{ "": {"type": "export", "target": "generate"}
, "generate":
  { "type": "generic"
  , "outs": ["TARGETS"]
  , "deps": ["generate.py"]
  , "cmds":
    [ { "type": "join"
      , "separator": " "
      , "$1":
        [ "python3"
        , "generate.py"
        , "10"
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

cat > repo-config.json <<EOF
{ "repositories":
    { "targets":
        { "repository":
            { "type": "file"
            , "path": "${TARGETS_DIR}"
            , "pragma": {"to_git": true}
            }
        }
    , "base":
        { "repository":
            { "type": "file"
            , "path": "${SRC_DIR}"
            , "pragma": {"to_git": true}
            }
        , "target_root": "targets"
        , "target_file_name": "TARGETS.collect"
        }
    , "computed":
        { "repository":
            { "type": "computed"
            , "repo": "base"
            , "target": ["", ""]
            }
        , "target_root": "targets"
        , "target_file_name": "TARGETS.compute"
        }
    }
}
EOF
cat repo-config.json

# Ensure just-mr handles "target_file_name" of computed roots properly.

echo
echo Building computed
echo
"${JUST_MR}" --norc --local-build-root "${LBRDIR}" -C repo-config.json \
            --main computed  --just "${JUST}" \
            install -L '["env", "PATH='"${PATH}"'"]' -o "${OUT}/base" 2>&1
echo

echo OK
