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


set -eu

readonly GIT_IMPORT="${PWD}/bin/git-import-under-test"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly REPO_DIRS="${TEST_TMPDIR}/repos"
readonly OUT="${TEST_TMPDIR}/out"
readonly LBR="${TEST_TMPDIR}/local-build-root"
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
git init 2>&1
git checkout --orphan foomaster 2>&1
git config user.name 'N.O.Body' 2>&1
git config user.email 'nobody@example.org' 2>&1
git add . 2>&1
git commit -m 'Add foo.txt' 2>&1


mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["bar.txt"]
  , "cmds": ["cat foo.txt | tr A-Z a-z > bar.txt"]
  , "deps": [["@", "foo", "", ""]]
  }
}
EOF
cat > repos.template.json <<'EOF'
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo"}
    }
  }
}
EOF

echo
echo Import
echo

"${GIT_IMPORT}" -C repos.template.json \
                --as foo -b foomaster \
                --mirror http://primary.example.org/foo \
                --inherit-env AUTH_VAR_FOO \
                --mirror http://secondary.example.org/foo \
                --inherit-env AUTH_VAR_BAR \
                "${REPO_DIRS}/foo" \
                > repos.json
echo
cat repos.json
echo


echo
echo Sanity check: can build
echo
"${JUST_MR}" --norc --just "${JUST}" \
             -L '["env", "PATH='"${PATH}"'"]' \
             -C repos.json \
             --local-build-root "${LBR}" \
             install -o "${OUT}" 2>&1

[ "$(cat ${OUT}/bar.txt)" = "foo" ]

echo
echo Build OK, verifying annotations
echo
jq '.repositories.foo.repository' repos.json > foo.json

echo Repository entry for foo
echo
cat foo.json
echo

[ $(jq '."inherit env" | . == ["AUTH_VAR_FOO", "AUTH_VAR_BAR"]' foo.json) = "true" ]
[ $(jq '."mirrors" | . == ["http://primary.example.org/foo", "http://secondary.example.org/foo"]' foo.json) = "true" ]

echo OK
