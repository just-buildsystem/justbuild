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

###
# This test checks that special entries, be it symlinks or git submodules, are
# properly ignored during setup of git roots when the "special":"ignore" pragma
# is provided.
##

set -eu

env

readonly ROOT="${PWD}"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/build-output"
readonly REPO_DIRS="${TEST_TMPDIR}/repos"
readonly WRKDIR="${PWD}/work"

# Create a Git repo 'foo'
mkdir -p "${REPO_DIRS}/foo"
cd "${REPO_DIRS}/foo"
git init
git checkout --orphan foomaster
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
touch data
git add . -f
git commit -m 'Init foo' 2>&1

# Create a Git repo 'foo' with a submodule
mkdir -p "${REPO_DIRS}/bar"
cd "${REPO_DIRS}/bar"
git init
git checkout --orphan barmaster
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git -c protocol.file.allow=always submodule add "${REPO_DIRS}/foo/.git" # submodule foo
ln -s ../nonexistent a_link  # a symlink to be ignored
echo '{"":{"type":"install","deps":[["TREE", null, "."]]}}' >TARGETS
git add . -f
git commit -m 'Init bar' 2>&1

# Main repo depending on repo 'bar'
mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
echo '{"":{"type":"install","deps":[["@", "bar", "", ""]]}}' >TARGETS

echo
echo Check git root fails without pragma:ignore
echo

cat > repos.json << EOF
{ "main": ""
, "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"bar": "bar"}
    }
  , "bar":
    { "repository":
      { "type": "file"
      , "path": "${REPO_DIRS}/bar"
      , "pragma": {"to_git": true}
      }
    }
  }
}
EOF

"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" build 2>&1 \
    && echo "this should fail" && exit 1
echo
echo "failed as expected"

cat > repos.json << EOF
{ "main": ""
, "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"bar": "bar"}
    }
  , "bar":
    { "repository":
      { "type": "file"
      , "path": "${REPO_DIRS}/bar"
      , "pragma": {"to_git": true, "special":"ignore"}
      }
    }
  }
}
EOF

echo
echo Check git root with pragma:ignore succeeds
echo
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" install -o "${OUT}" 2>&1

[ ! -e "${OUT}/a_link" ]     # symlink should not be there
[ ! -e "${OUT}/foo" ]        # submodule should not be there

echo OK
