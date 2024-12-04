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
readonly JUST_LOCK="${PWD}/bin/lock-tool-under-test"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/build-output"
readonly REPO_DIRS="${TEST_TMPDIR}/repos"
readonly WRKDIR="${PWD}/work"

mkdir -p "${REPO_DIRS}/foo/src"
cd "${REPO_DIRS}/foo"
cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": "src"}}}}
EOF
cat > src/TARGETS <<'EOF'
{ "": {"type": "file_gen", "name": "foo.txt", "data": "FOO"}}
EOF
git init
git checkout --orphan foomaster
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git add .
git commit -m 'Add foo.txt' 2>&1


mkdir -p "${REPO_DIRS}/bar"
cd "${REPO_DIRS}/bar"
cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": ""}}}}
EOF
cat > TARGETS <<'EOF'
{ "": {"type": "file_gen", "name": "bar.txt", "data": "BAR"}}
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
cat > repos.in.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo", "bar": "bar"}
    }
  }
  , "imports":
  [ { "source": "git"
    , "repos": [{"alias": "foo"}]
    , "url": "${REPO_DIRS}/foo"
    , "branch": "foomaster"
    }
  , { "source": "git"
    , "repos": [{"alias": "bar"}]
    , "url": "${REPO_DIRS}/bar"
    , "branch": "barmaster"
    }
  ]
}
EOF
echo
cat repos.in.json

"${JUST_LOCK}" -C repos.in.json -o repos.json

echo
cat repos.json
echo
"${JUST_MR}" -L '["env", "PATH='"${PATH}"'"]' --norc --just "${JUST}" --local-build-root "${LBR}" install -o "${OUT}" 2>&1
echo
cat "${OUT}/out.txt"
echo
grep -q FOOBAR "${OUT}/out.txt"

# Verify that results match a chained import, as no deduplication is to be done
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
    | "${GIT_IMPORT}" -C - --as bar -b barmaster "${REPO_DIRS}/bar" > repos-chained.json
jq . repos.json > repos.json
jq . repos-chained.json > repos-chained.json
diff repos.json repos-chained.json

echo "OK"
