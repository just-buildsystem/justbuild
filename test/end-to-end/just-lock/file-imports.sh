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

readonly JUST_LOCK="${PWD}/bin/lock-tool-under-test"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/build-output"
readonly REPO_DIRS="${TEST_TMPDIR}/repos"
readonly WRKDIR="${PWD}/work"

mkdir -p "${REPO_DIRS}/foo/src/inner"
cd "${REPO_DIRS}/foo"
cat > repos.json <<'EOF'
{ "repositories":
  { "":
    { "repository":
      { "type": "file"
      , "path": "src"
      , "pragma": {"special": "resolve-completely"}
      }
    }
  }
}
EOF
# add symlink to check that special pragma is needed and is inherited in import
ln -s inner/actual_TARGETS src/TARGETS
cat > src/inner/actual_TARGETS <<'EOF'
{ "": {"type": "file_gen", "name": "foo.txt", "data": "FOO"}}
EOF


mkdir -p "${REPO_DIRS}/bar"
cd "${REPO_DIRS}/bar"
cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": ""}}}}
EOF
cat > TARGETS <<'EOF'
{ "": {"type": "file_gen", "name": "bar.txt", "data": "BAR"}}
EOF

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

# Import repos as local checkouts
cat > repos.in.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo", "bar": "bar"}
    }
  }
  , "imports":
  [ { "source": "file"
    , "repos": [{"alias": "foo", "pragma": {"to_git": true}}]
    , "path": "${REPO_DIRS}/foo"
    }
  , { "source": "file"
    , "repos": [{"alias": "bar"}]
    , "path": "${REPO_DIRS}/bar"
    }
  ]
}
EOF
echo
cat repos.in.json

echo
"${JUST_LOCK}" -C repos.in.json -o repos.json --local-build-root "${LBR}" 2>&1
cat repos.json
echo
# Check pragmas for repo "foo" are correctly set
[ $(jq -r '.repositories.foo.repository.pragma.special' repos.json) = "resolve-completely" ]
[ $(jq -r '.repositories.foo.repository.pragma.to_git' repos.json) = true ]
# Check the symlink gets resolved as expected
"${JUST_MR}" -L '["env", "PATH='"${PATH}"'"]' --norc --just "${JUST}" --local-build-root "${LBR}" install -o "${OUT}" 2>&1
echo
cat "${OUT}/out.txt"
echo
grep -q FOOBAR "${OUT}/out.txt"

echo "OK"
