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
  , "derived":
    {"workspace_root": ["computed", "base", "", "", {"COUNT": "10"}]}
  , "other derived":
    {"workspace_root": ["computed", "base", "", "", {"COUNT": "12"}]}
  }
}
EOF
cat repo-config.json

echo
echo Building base, for reference
echo
"${JUST}" install -L '["env", "PATH='"${PATH}"'"]' \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    --main base -D '{"COUNT": "10"}' -o "${OUT}/base" 2>&1
echo
cat "${OUT}/base/TARGETS"

echo
echo Building computed
echo
"${JUST}" install -L '["env", "PATH='"${PATH}"'"]' \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    --log-limit 4 --main derived -o "${OUT}/derived" 2>&1
echo

[ "$(cat "${OUT}/derived/out" | wc -l)" -eq 55 ]

echo
echo Building a different computed root, without reference build
echo
"${JUST}" install -L '["env", "PATH='"${PATH}"'"]' \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    --log-limit 4 -f "${OUT}/log" \
    --main 'other derived' -o "${OUT}/other-derived" 2>&1
echo

[ "$(cat "${OUT}/other-derived/out" | wc -l)" -eq 78 ]

echo
echo Sanity-check of the log
echo
grep '[Rr]oot.*base.*evaluted.*' "${OUT}/log"  > "${TMPDIR}/log_line"
cat "${TMPDIR}/log_line"
sed -i 's/.*log //' "${TMPDIR}/log_line"
"${JUST}" install-cas --local-build-root "${LBRDIR}" \
    -o "${OUT}/log.root" $(cat "${TMPDIR}/log_line")
echo
cat "${OUT}/log.root"
echo
grep 'COUNT.*12' "${OUT}/log.root"
grep '[Dd]iscovered.*1 action' "${OUT}/log.root"
grep '0 cache hit' "${OUT}/log.root"

echo
echo Building computed root again, expecting target-level cache hit
echo
"${JUST}" build -L '["env", "PATH='"${PATH}"'"]' \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    --log-limit 4 -f "${OUT}/log2" \
    --main 'other derived' 2>&1
echo
grep '[Ee]xport.*from cache' "${OUT}/log2"

echo OK
