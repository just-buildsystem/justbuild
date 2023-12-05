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

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly LOCAL_TMPDIR="${TEST_TMPDIR}/local-tmpdir"
readonly INSTALL_DIR="${TEST_TMPDIR}/install-dir"

# A standard remote-execution server is given by the test infrastructure
REMOTE_EXECUTION_ARGS="-r ${REMOTE_EXECUTION_ADDRESS}"

# Set up folder
mkdir -p bin
cat > bin/mock-tree <<EOF
#!/bin/sh
mkdir -p foo/bar
echo "test data" > foo/bar/data.txt
EOF
chmod 755 bin/mock-tree
export PATH="${PWD}/bin:${PATH}"

# get the tree identifier
mkdir -p work
( cd work
  mock-tree
  git init
  git config user.email "nobody@example.org"
  git config user.name "Nobody"
  git add -f .
  git commit -m "Sample output" 2>&1
)
readonly TREE_ID=$(cd work && git log -n 1 --format='%T')
rm -rf work
echo "dir as tree ${TREE_ID}"

# Setup sample repository config
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "git tree"
      , "id": "${TREE_ID}"
      , "cmd": ["mock-tree"]
      , "inherit env": ["PATH"]
      }
    }
  }
}
EOF
cat repos.json

# Fetch while backing up to remote
"${JUST_MR}" --norc --local-build-root "${LBR}" \
             ${REMOTE_EXECUTION_ARGS} fetch -o "${LOCAL_TMPDIR}" \
             --backup-to-remote 2>&1

# Check that tree was fetched
"${JUST}" install-cas --local-build-root "${LBR}" -o "${INSTALL_DIR}" \
          "${TREE_ID}::t" 2>&1
test "$(cat "${INSTALL_DIR}/foo/bar/data.txt")" = "test data"

# Clean build root and dirs
rm -rf "${LBR}"
rm -rf "${LOCAL_TMPDIR}"
rm -rf "${INSTALL_DIR}"

# Now remove the generating script from the repository description
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "git tree"
      , "id": "${TREE_ID}"
      , "cmd": ["non_existent_script.sh"]
      }
    }
  }
}
EOF
cat repos.json

# Fetch using only the remote backup
"${JUST_MR}" --norc --local-build-root "${LBR}" \
             ${REMOTE_EXECUTION_ARGS} fetch -o "${LOCAL_TMPDIR}" 2>&1

# Check that tree was fetched
"${JUST}" install-cas --local-build-root "${LBR}" -o "${INSTALL_DIR}" \
          "${TREE_ID}::t" 2>&1
test "$(cat "${INSTALL_DIR}/foo/bar/data.txt")" = "test data"


echo OK
