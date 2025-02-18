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


set -eu

readonly JUST_LOCK="${PWD}/bin/lock-tool-under-test"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly LBR_PLAIN="${TEST_TMPDIR}/local-build-root-plain"
readonly OUT="${TEST_TMPDIR}/build-output"
readonly OUT_PLAIN="${TEST_TMPDIR}/build-output-plain"
readonly REPO_DIRS="${TEST_TMPDIR}/repos"
readonly WRKDIR="${PWD}/work"

mkdir -p "${REPO_DIRS}/foo/inner"
cd "${REPO_DIRS}/foo"
touch ROOT
cat > repos.json <<'EOF'
{ "repositories":
  { "":
    { "repository":
      { "type": "file"
      , "path": "inner"
      }
    }
  }
}
EOF
# add resolvable linked TARGETS file
ln -s inner/linked_TARGETS TARGETS
cat > inner/linked_TARGETS <<'EOF'
{ "": {"type": "file_gen", "name": "foo.txt", "data": "LINK"}}
EOF
# add inner TARGETS file shadowed by the linked one
cat > inner/TARGETS << 'EOF'
{ "": {"type": "file_gen", "name": "foo.txt", "data": "INNER"}}
EOF

mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "cmds": ["cat foo.txt > out.txt"]
  , "outs": ["out.txt"]
  , "deps": [["@", "foo", "", ""]]
  }
}
EOF


echo === Check normal import ===

cat > repos.in.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo"}
    }
  }
  , "imports":
  [ { "source": "file"
    , "repos": [{"alias": "foo", "pragma": {"to_git": true}}]
    , "path": "${REPO_DIRS}/foo"
    }
  ]
}
EOF
cat repos.in.json

echo
"${JUST_LOCK}" -C repos.in.json -o repos.json --local-build-root "${LBR}" 2>&1
cat repos.json
echo
# Check pragmas: "to_git" is kept
[ $(jq -r '.repositories.foo.repository.pragma.to_git' repos.json) = true ]
# Check that the subdir is taken as expected
"${JUST_MR}" -L '["env", "PATH='"${PATH}"'"]' --norc --just "${JUST}" \
    --local-build-root "${LBR}" install -o "${OUT}" 2>&1
echo
cat "${OUT}/out.txt"
echo
grep -q INNER "${OUT}/out.txt"


echo == Check plain import ===

cat > repos.in.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo"}
    }
  }
  , "imports":
  [ { "source": "file"
    , "repos": [{"alias": "foo", "pragma": {"to_git": true}}]
    , "path": "${REPO_DIRS}/foo"
    , "as plain": true
    , "pragma": {"special": "resolve-completely"}
    }
  ]
}
EOF
cat repos.in.json

echo
"${JUST_LOCK}" -C repos.in.json -o repos.json --local-build-root "${LBR_PLAIN}" 2>&1
cat repos.json
echo
# Check pragmas: "to_git" is kept, "special" is unconditionally set
[ $(jq -r '.repositories.foo.repository.pragma.special' repos.json) = "resolve-completely" ]
[ $(jq -r '.repositories.foo.repository.pragma.to_git' repos.json) = true ]
# Check the symlink gets resolved as expected
"${JUST_MR}" -L '["env", "PATH='"${PATH}"'"]' --norc --just "${JUST}" \
    --local-build-root "${LBR_PLAIN}" install -o "${OUT_PLAIN}" 2>&1
echo
cat "${OUT_PLAIN}/out.txt"
echo
grep -q LINK "${OUT_PLAIN}/out.txt"

echo "OK"
