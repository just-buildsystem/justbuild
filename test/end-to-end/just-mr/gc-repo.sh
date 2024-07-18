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

readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly WORK="${PWD}/work"
readonly GIT_REPO="${PWD}/git-repository"
readonly BIN="${TEST_TMPDIR}/bin"
mkdir -p "${BIN}"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/out"
mkdir -p "${OUT}"
readonly LOG="${PWD}/log"
mkdir -p "${LOG}"

# Set up git repo
mkdir -p "${GIT_REPO}"
cd "${GIT_REPO}"
mkdir data
echo Hello world > data/text
seq 1 10 > data/numbers
git init 2>&1
git branch -m stable-1.0 2>&1
git config user.email "nobody@example.org" 2>&1
git config user.name "Nobody" 2>&1
git add . 2>&1
git commit -m "Sample data" 2>&1
git show
GIT_COMMIT=$(git log -n 1 --format="%H")
GIT_TREE=$(git log -n 1 --format="%T")
echo
echo "Created commit ${GIT_COMMIT} with tree ${GIT_TREE}"
echo

## As we simulate remote fetching, create a mock git binary
cat > "${BIN}/mock-git" <<EOF
#!/bin/sh
if [ "\$1" = "fetch" ]
then
  git fetch "${GIT_REPO}" stable-1.0
else
  git "\$@"
fi
EOF
chmod 755 "${BIN}/mock-git"

# Create workspace with just-mr repository configuration
mkdir -p "${WORK}"
cd "${WORK}"
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    {"repository": {"type": "file", "path": "."}, "bindings": {"git": "git"}}
  , "git":
    { "repository":
      { "type": "git"
      , "commit": "${GIT_COMMIT}"
      , "repository": "mockprotocol://example.com/repo"
      , "branch": "stable-1.0"
      }
    }
  }
}
EOF
cat repos.json

# Set up repos. This should get everything in the local build root.
"${JUST_MR}" --local-build-root "${LBR}" --git "${BIN}/mock-git" \
   setup  > "${OUT}/conf-file-name" 2> "${LOG}/log-1"
cat "${LOG}/log-1"
echo
CONFIG="$(cat "${OUT}/conf-file-name")"
cat "${CONFIG}"
echo

## Verify git repo is set up correctly
TREE_FOUND="$(jq -r '.repositories.git.workspace_root[1]' "${CONFIG}")"
[ "${TREE_FOUND}" = "${GIT_TREE}" ]
GIT_LOCATION="$(jq -r '.repositories.git.workspace_root[2]' "${CONFIG}")"

# Clean up original repositories
rm -rf "${GIT_REPO}"

# Rotate repo cache
"${JUST_MR}" --local-build-root "${LBR}" gc-repo 2>&1

## Verify the mirrored locations are gone
[ -e "${GIT_LOCATION}" ] && exit 1 || :

# Setup repos again
"${JUST_MR}" --local-build-root "${LBR}" \
    setup  > "${OUT}/conf-file-name" 2> "${LOG}/log-2"
cat "${LOG}/log-2"
echo
CONFIG="$(cat "${OUT}/conf-file-name")"
cat "${CONFIG}"
echo

## Verify git repo is set up correctly
TREE_FOUND="$(jq -r '.repositories.git.workspace_root[1]' "${CONFIG}")"
[ "${TREE_FOUND}" = "${GIT_TREE}" ]


echo OK
