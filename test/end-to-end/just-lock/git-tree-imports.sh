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
readonly LBR2="${TEST_TMPDIR}/local-build-root-2"
readonly LBR3="${TEST_TMPDIR}/local-build-root-3"
readonly LBR4="${TEST_TMPDIR}/local-build-root-4"
readonly OUT="${TEST_TMPDIR}/build-output"
readonly REPO_DIRS="${TEST_TMPDIR}/repos"
readonly WRKDIR="${PWD}/work"

# Git-tree generator and its own generator
mkdir -p bin
cat > bin/mock-vcs <<'EOF'
#!/bin/sh
if [ "$(cat ${CREDENTIAL_PATH:-/dev/null})" = "sEcReT" ]
then
  mkdir -p data
  echo "$1" > data/sources.txt
  echo '{"":{"type":"install","dirs":[[["TREE",null,"."],"."]]}}' > data/TARGETS
else
  echo 'not enough credentials available'
fi
EOF
chmod 755 bin/mock-vcs
cat > bin/mock-vcs-gen <<'EOF'
#!/bin/sh
echo '["mock-vcs", "checkout"]'
EOF
chmod 755 bin/mock-vcs-gen
export PATH="$(pwd)/bin:${PATH}"

mkdir -p etc
echo -n sEcReT > etc/pass
export CREDENTIAL_PATH="$(pwd)/etc/pass"

# Compute tree of our mock checkout
mkdir -p temp
( cd temp
  mock-vcs "checkout" 2>&1
  git init 2>&1
  git config user.email "nobody@example.org" 2>&1
  git config user.name "Nobody" 2>&1
  git add . 2>&1
  git commit -m "Sample output" 2>&1
)
readonly TREE_ID=$(cd temp && git log -n 1 --format="%T")
echo "Tree of checkout is ${TREE_ID}."
readonly SUBTREE_ID=$(cd temp && git rev-parse "${TREE_ID}":data)
echo "Tree of data is ${SUBTREE_ID}"
rm -rf temp

# Main repo
mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "cmds": ["cat sources.txt > out.txt"]
  , "outs": ["out.txt"]
  , "deps": [["@", "foo", "", ""]]
  }
}
EOF
cat > repos.in.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo"}
    }
  }
  , "imports":
  [ { "source": "git tree"
    , "repos": [{"alias": "foo"}]
    , "cmd": ["mock-vcs", "checkout"]
    , "inherit env": ["PATH", "CREDENTIAL_PATH"]
    , "subdir": "data"
    , "as plain": true
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

# Check the expected Git tree identifier of the subdir in the configuration
grep -q "${SUBTREE_ID}" repos.json

# Check command generation
cat > repos.in.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo"}
    }
  }
  , "imports":
  [ { "source": "git tree"
    , "repos": [{"alias": "foo"}]
    , "cmd gen": ["mock-vcs-gen"]
    , "inherit env": ["PATH", "CREDENTIAL_PATH"]
    , "subdir": "data"
    , "as plain": true
    }
  ]
}
EOF
echo
cat repos.in.json

echo
"${JUST_LOCK}" -C repos.in.json -o repos-gen.json --local-build-root "${LBR2}" 2>&1
cat repos-gen.json
echo

# Check the same Git tree identifier of the subdir is in the configuration
grep -q "${SUBTREE_ID}" repos-gen.json

# Check successful build
"${JUST_MR}" --norc -L '["env", "PATH='"${PATH}"'"]' --just "${JUST}" \
             --local-build-root "${LBR3}" install -o "${OUT}" 2>&1
echo
grep checkout "${OUT}/out.txt"
echo

# Verify the environment is needed
export CREDENTIAL_PATH=/dev/null
"${JUST_MR}" --norc -L '["env", "PATH='"${PATH}"'"]' \
             --local-build-root "${LBR4}" setup 2>&1 && exit 1 || :
echo
echo "failed as expected"
echo

echo "OK"
