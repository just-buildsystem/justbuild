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
readonly LBR_1="${TEST_TMPDIR}/local-build-root-1"
readonly LBR_2="${TEST_TMPDIR}/local-build-root-2"

readonly OUT_1="${TEST_TMPDIR}/build-output-1"
readonly OUT_2="${TEST_TMPDIR}/build-output-2"

readonly REPO_DIRS="${TEST_TMPDIR}/repos"
readonly WRKDIR="${PWD}/work"

# Repository 'foo'
mkdir -p "${REPO_DIRS}/foo/src"
cd "${REPO_DIRS}/foo"
cat > repos.json <<'EOF'
{ "repositories":
  { "":
    { "repository":
      { "type": "file"
      , "path": "src"
      , "pragma": {"special": "ignore"}
      }
    }
  }
}
EOF
cat > src/TARGETS <<'EOF'
{ "": {"type": "file_gen", "name": "foo.txt", "data": "FOO"}}
EOF
# add symlink to check that special pragma is needed and is inherited in import
ln -s ../../../nonexistent src/to_ignore
git init
git checkout --orphan foomaster
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git add .
git commit -m 'Add foo.txt' 2>&1

# Repository 'bar' depending on 'foo'
mkdir -p "${REPO_DIRS}/bar"
cd "${REPO_DIRS}/bar"
cat > repos.in.json <<EOF
{ "repositories":
  { "":
    {"repository": {"type": "file", "path": ""}, "bindings": {"foo": "foo"}}
  }
, "imports":
  [ { "source": "git"
    , "repos": [{"alias": "foo"}]
    , "url": "${REPO_DIRS}/foo"
    , "branch": "foomaster"
    }
  ]
}
EOF
echo
echo Import foo repo:
"${JUST_LOCK}" -C repos.in.json -o repos.json --local-build-root "${LBR}" 2>&1
cat repos.json
echo
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "cmds": ["cat foo.txt bar.txt > foobar.txt"]
  , "outs": ["foobar.txt"]
  , "deps": [["@", "foo", "", ""], "internal"]
  }
, "internal": {"type": "file_gen", "name": "bar.txt", "data": "BAR"}
}
EOF
git init
git checkout --orphan barmaster
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git add .
git commit -m 'Add bar.txt' 2>&1

# Main repository depending on 'bar'
mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "cmds": ["cat foobar.txt > out.txt"]
  , "outs": ["out.txt"]
  , "deps": [["@", "bar", "", ""]]
  }
}
EOF
cat > repos.in.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"bar": "bar"}
    }
  }
  , "imports":
  [ { "source": "git"
    , "repos": [{"alias": "bar"}]
    , "url": "${REPO_DIRS}/bar"
    , "branch": "barmaster"
    }
  ]
}
EOF
cat repos.in.json
echo
echo Import bar repo:
"${JUST_LOCK}" -C repos.in.json -o repos.json --local-build-root "${LBR}" 2>&1
cat repos.json
echo

echo
echo Check main repo build:
"${JUST_MR}" -L '["env", "PATH='"${PATH}"'"]' --norc --just "${JUST}" \
             --local-build-root "${LBR_1}" install -o "${OUT_1}" 2>&1
echo
cat "${OUT_1}/out.txt"
echo
grep -q FOOBAR "${OUT_1}/out.txt"

echo
echo Clone dependency foo of repository bar:
CLONE_TO="cloned_foo"
"${JUST_LOCK}" -C repos.in.json -o repos.json \
               --clone '{"'${CLONE_TO}'": ["bar", ["foo"]]}' \
               --local-build-root "${LBR}" 2>&1
echo
cat repos.json
echo

echo Check output configuration:
grep "${CLONE_TO}" repos.json
grep pragma repos.json

[ -f "${CLONE_TO}/repos.json" ] && [ -d "${CLONE_TO}/src" ] \
    && [ -f "${CLONE_TO}/src/TARGETS" ] && [ -L "${CLONE_TO}/src/to_ignore" ] || exit 1

echo
echo Check build with cloned repo:
"${JUST_MR}" -L '["env", "PATH='"${PATH}"'"]' --norc --just "${JUST}" \
             --local-build-root "${LBR_2}" install -o "${OUT_2}" 2>&1
echo
cat "${OUT_1}/out.txt"
echo
grep -q FOOBAR "${OUT_1}/out.txt"
echo

echo OK
