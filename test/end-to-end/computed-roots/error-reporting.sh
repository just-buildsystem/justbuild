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
  , "file base": {"workspace_root": ["file", "${BASE_ROOT}"]}
  , "derived":
    {"workspace_root": ["computed", "base", "", "", {"COUNT": "10"}]}
  , "not export":
    {"workspace_root": ["computed", "base", "", "generate", {"COUNT": "10"}]}
  , "not content-fixed":
    {"workspace_root": ["computed", "file base", "", "", {"COUNT": "10"}]}
  , "cycle-A":
    {"workspace_root": ["computed", "cycle-B", "", "", {}]}
  , "cycle-B":
    {"workspace_root": ["computed", "cycle-C", "", "", {}]}
  , "cycle-C":
    {"workspace_root": ["computed", "cycle-A", "", "", {}]}
  }
}
EOF
cat repo-config.json

echo
echo Building computed
echo
"${JUST}" install -L '["env", "PATH='"${PATH}"'"]' \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    --log-limit 4 --main derived -o "${OUT}/derived" 2>&1
echo

[ "$(cat "${OUT}/derived/out" | wc -l)" -eq 55 ]

echo
echo Not an export
echo
"${JUST}" build -L '["env", "PATH='"${PATH}"'"]' \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    -f "${OUT}/not-export.log" \
    --log-limit 4 --main 'not export' 2>&1 && exit 1 || :
echo
grep '[Tt]arget.*not.*export' "${OUT}/not-export.log"

echo
echo Not content fixed
echo
"${JUST}" build -L '["env", "PATH='"${PATH}"'"]' \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    -f "${OUT}/not-content-fixed.log" \
    --log-limit 4 --main 'not content-fixed' 2>&1 && exit 1 || :
echo
grep 'Repository.*file base.*not.*content.*fixed' "${OUT}/not-content-fixed.log"

echo
echo cycle
echo
"${JUST}" build -L '["env", "PATH='"${PATH}"'"]' \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    -f "${OUT}/cycle.log" \
    --log-limit 4 --main 'cycle-A' 2>&1 && exit 1 || :
echo

grep cycle-A "${OUT}/cycle.log"
grep cycle-B "${OUT}/cycle.log"
grep cycle-C "${OUT}/cycle.log"

echo OK
