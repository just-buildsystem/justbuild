#!/bin/sh
# Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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
readonly OUT="${ROOT}/out"


readonly BASE_ROOT="${ROOT}/base"
mkdir -p "${BASE_ROOT}"
cd "${BASE_ROOT}"
cat > generate.py <<'EOF'
# A deliberately slow action for generating a root
import json
import sys
import time

COUNT = int(sys.argv[1])
targets = {}
for i in range(COUNT):
  time.sleep(1)
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
  { "BaSeRooT":
    {"workspace_root": ["git tree", "${GIT_TREE}", "${BASE_ROOT}/.git"]}
  , "derived-10":
    {"workspace_root": ["computed", "BaSeRooT", "", "", {"COUNT": "10"}]}
  , "derived-11":
    {"workspace_root": ["computed", "BaSeRooT", "", "", {"COUNT": "11"}]}
  , "derived-12":
    {"workspace_root": ["computed", "BaSeRooT", "", "", {"COUNT": "12"}]}
  , "derived-13":
    {"workspace_root": ["computed", "BaSeRooT", "", "", {"COUNT": "13"}]}
  , "":
    { "workspace_root": ["file", "."]
    , "bindings":
      { "a": "derived-10"
      , "b": "derived-11"
      , "c": "derived-12"
      , "d": "derived-13"
      }
    }
  }
}
EOF
cat repo-config.json
echo
cat > TARGETS <<'EOF'
{ "":
  { "type": "install"
  , "dirs":
    [ [["@", "a", "", ""], "a"]
    , [["@", "b", "", ""], "b"]
    , [["@", "c", "", ""], "c"]
    , [["@", "d", "", ""], "d"]
    ]
  }
}
EOF

echo
echo Building
echo
"${JUST}" install -L '["env", "PATH='"${PATH}"'"]' \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    -f "${OUT}/log" -o "${OUT}/out" 2>&1
echo

# Sanity check for build
[ "$(cat "${OUT}/out/a/out" | wc -l)" -eq 55 ]
[ "$(cat "${OUT}/out/b/out" | wc -l)" -eq 66 ]
[ "$(cat "${OUT}/out/c/out" | wc -l)" -eq 78 ]
[ "$(cat "${OUT}/out/d/out" | wc -l)" -eq 91 ]
echo

# Progress should mention at least one target of a computed root
grep 'PROG.*BaSeRooT' "${OUT}/log"

echo OK
