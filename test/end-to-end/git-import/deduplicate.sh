#!/bin/sh
# Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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


set -eu

readonly DEDUPLICATE="${PWD}/bin/deduplicate-tool-under-test"
readonly GIT_IMPORT="${PWD}/bin/git-import-under-test"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly ACTIONS_EQUAL="${PWD}/bin/actions-graph-equal"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/build-output"
readonly REPO_DIRS="${TEST_TMPDIR}/repos"
readonly WRKDIR="${PWD}"

mkdir -p "${REPO_DIRS}/foo/src"
cd "${REPO_DIRS}/foo"
cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": "src"}}}}
EOF
cat > src/TARGETS <<'EOF'
{ "":
  {"type": "generic", "outs": ["foo.txt"], "cmds": ["echo -n FOO > foo.txt"]}
}
EOF
git init
git checkout --orphan foomaster
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git add .
git commit -m 'Add foo.txt' 2>&1


mkdir -p "${REPO_DIRS}/bar"
cd "${REPO_DIRS}/bar"
cat > repos.template.json <<'EOF'
{ "repositories":
  { "":
    {"repository": {"type": "file", "path": ""}, "bindings": {"foo": "foo"}}
  }
}
EOF
"${GIT_IMPORT}" -C repos.template.json \
  --as foo -b foomaster "${REPO_DIRS}/foo" > repos.json
cat repos.json
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["bar.txt"]
  , "cmds": ["cat foo.txt | tr A-Z a-z > bar.txt"]
  , "deps": [["@", "foo", "", ""]]
  }
}
EOF
git init
git checkout --orphan barmaster
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git add .
git commit -m 'Add foo.txt' 2>&1

mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "cmds": ["cat foo.txt bar.txt > out.txt"]
  , "outs": ["out.txt"]
  , "deps": [["@", "foo", "", ""], ["@", "bar", "", ""]]
  }
}
EOF
cat > repos.template.json <<'EOF'
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo", "bar": "bar"}
    }
  }
}
EOF
"${GIT_IMPORT}" -C repos.template.json --as foo -b foomaster "${REPO_DIRS}/foo" \
    | "${GIT_IMPORT}" -C - --as bar -b barmaster "${REPO_DIRS}/bar" \
    > repos-full.json

echo
cat repos-full.json
echo
"${JUST_MR}" -C repos-full.json --norc --just "${JUST}" \
             --local-build-root "${LBR}" analyse \
             --dump-graph actions-full.json 2>&1
echo
cat repos-full.json | "${DEDUPLICATE}" > repos.json
cat repos.json
echo

"${JUST_MR}" -C repos.json --norc --just "${JUST}" \
             --local-build-root "${LBR}" analyse \
             --dump-graph actions.json 2>&1

# Verify that we reduced the number of repositories, but did
# not change the action graph (except for the origins of the actions).
[ $(jq -aM '.repositories | length' repos.json) -lt $(jq -aM '.repositories | length' repos-full.json) ]
"${ACTIONS_EQUAL}" actions-full.json actions.json

echo "OK"
