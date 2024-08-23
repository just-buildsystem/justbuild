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

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly WORK="${PWD}/work"
readonly GIT_REPO="${PWD}/git-repository"
readonly BIN="${TEST_TMPDIR}/bin"
mkdir -p "${BIN}"
readonly DISTDIR="${PWD}/distdir"
mkdir -p "${DISTDIR}"
readonly ARCHIVE_CONTENT="${TEST_TMPDIR}/archive"
readonly SCRATCH_LBR="${TEST_TMPDIR}/throw-away-build-root"
readonly SCRATCH_FF="${TEST_TMPDIR}/scratch-space-for-foreign-file"
readonly SCRATCH_TREE="${TEST_TMPDIR}/scratch-space-for-git-tree"
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

# Set up archive

mkdir -p "${ARCHIVE_CONTENT}"
cd "${ARCHIVE_CONTENT}"
echo archive content > description.txt
seq 1 20 > data
ARCHIVE_TREE=$("${JUST}" add-to-cas --local-build-root "${SCRATCH_LBR}" .)
tar cvf "${DISTDIR}/archive.tar" .
ARCHIVE=$("${JUST}" add-to-cas --local-build-root "${SCRATCH_LBR}" "${DISTDIR}/archive.tar")
echo
echo "Created archive with content ${ARCHIVE} holding tree ${ARCHIVE_TREE}"
echo

# Set up a "foreign file"

echo Here be dragons > "${DISTDIR}/foreign.txt"
FOREIGN=$("${JUST}" add-to-cas --local-build-root "${SCRATCH_LBR}" "${DISTDIR}/foreign.txt")
rm -rf "${SCRATCH_FF}"
mkdir -p "${SCRATCH_FF}"
cp "${DISTDIR}/foreign.txt" "${SCRATCH_FF}/data.txt"
FOREIGN_TREE=$("${JUST}" add-to-cas --local-build-root "${SCRATCH_LBR}" "${SCRATCH_FF}")
echo
echo "Foreign file ${FOREIGN} creating tree ${FOREIGN_TREE}"
echo

# Set up a "git tree"
cat > "${BIN}/mock-foreign-vcs" <<'EOF'
#!/bin/sh

mkdir -p foo/bar/baz
echo foo data > foo/data.txt
echo bar data > foo/bar/data.txt
echo baz data > foo/bar/baz/data.txt
EOF
chmod 755 "${BIN}/mock-foreign-vcs"
rm -rf "${SCRATCH_TREE}"
mkdir -p "${SCRATCH_TREE}"
(cd "${SCRATCH_TREE}" && "${BIN}/mock-foreign-vcs")
FOREIGN_GIT_TREE=$("${JUST}" add-to-cas --local-build-root "${SCRATCH_LBR}" "${SCRATCH_TREE}")
echo
echo "Git tree ${FOREIGN_GIT_TREE}"
echo

# Create workspace with just-mr repository configuration
mkdir -p "${WORK}"
cd "${WORK}"
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings":
      { "git": "git"
      , "archive": "archive"
      , "foreign": "foreign_file"
      , "tree": "git_tree"
      }
    }
  , "git":
    { "repository":
      { "type": "git"
      , "commit": "${GIT_COMMIT}"
      , "repository": "mockprotocol://example.com/repo"
      , "branch": "stable-1.0"
      }
    }
  , "archive":
    { "repository":
      { "type": "archive"
      , "content": "${ARCHIVE}"
      , "fetch": "http://nonexistent.example.com/archive.tar"
      }
    }
  , "foreign_file":
    { "repository":
      { "type": "foreign file"
      , "content": "${FOREIGN}"
      , "fetch": "http://nonexistent.example.com/foreign.txt"
      , "name": "data.txt"
      }
    }
  , "git_tree":
    { "repository":
      { "type": "git tree"
      , "id": "${FOREIGN_GIT_TREE}"
      , "cmd": ["${BIN}/mock-foreign-vcs"]
      }
    }
  }
}
EOF
cat repos.json


# Set up repos. This should get everything in the local build root.
"${JUST_MR}" --local-build-root "${LBR}" --git "${BIN}/mock-git" \
             --distdir "${DISTDIR}" -L '["env", "PATH='"${PATH}"'"]' \
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

## Verify the archive is set up correctly
TREE_FOUND="$(jq -r '.repositories.archive.workspace_root[1]' "${CONFIG}")"
[ "${TREE_FOUND}" = "${ARCHIVE_TREE}" ]
ARCHIVE_LOCATION="$(jq -r '.repositories.archive.workspace_root[2]' "${CONFIG}")"

## Sanity check for foreign file
TREE_FOUND="$(jq -r '.repositories.foreign_file.workspace_root[1]' "${CONFIG}")"
[ "${TREE_FOUND}" = "${FOREIGN_TREE}" ]
FOREIGN_LOCATION="$(jq -r '.repositories.foreign_file.workspace_root[2]' "${CONFIG}")"

## Sanity check for git tree
TREE_FOUND="$(jq -r '.repositories.git_tree.workspace_root[1]' "${CONFIG}")"
[ "${TREE_FOUND}" = "${FOREIGN_GIT_TREE}" ]
GIT_TREE_LOCATION="$(jq -r '.repositories.git_tree.workspace_root[2]' "${CONFIG}")"

# Clean up original repositories
rm -rf "${GIT_REPO}"
rm -rf "${DISTDIR}"
rm -f "${BIN}/mock-foreign-vcs"

# Fully clean the non-repo cache
"${JUST}" gc --local-build-root "${LBR}" 2>&1
"${JUST}" gc --local-build-root "${LBR}" 2>&1

# Rotate repo cache
"${JUST_MR}" --local-build-root "${LBR}" gc-repo 2>&1

## Verify the mirrored locations are gone
[ -e "${GIT_LOCATION}" ] && exit 1 || :
[ -e "${ARCHIVE_LOCATION}" ] && exit 1 || :
[ -e "${FOREIGN_LOCATION}" ] && exit 1 || :
[ -e "${GIT_TREE_LOCATION}" ] && exit 1 || :

# Setup repos again
"${JUST_MR}" --local-build-root "${LBR}" -L '["env", "PATH='"${PATH}"'"]' \
    setup  > "${OUT}/conf-file-name" 2> "${LOG}/log-2"
cat "${LOG}/log-2"
echo
CONFIG="$(cat "${OUT}/conf-file-name")"
cat "${CONFIG}"
echo

## Verify git repo is set up correctly
TREE_FOUND="$(jq -r '.repositories.git.workspace_root[1]' "${CONFIG}")"
[ "${TREE_FOUND}" = "${GIT_TREE}" ]

## Verify the archive is set up correctly
TREE_FOUND="$(jq -r '.repositories.archive.workspace_root[1]' "${CONFIG}")"
[ "${TREE_FOUND}" = "${ARCHIVE_TREE}" ]

## Sanity check for foreign file
TREE_FOUND="$(jq -r '.repositories.foreign_file.workspace_root[1]' "${CONFIG}")"
[ "${TREE_FOUND}" = "${FOREIGN_TREE}" ]

## Sanity check for git tree
TREE_FOUND="$(jq -r '.repositories.git_tree.workspace_root[1]' "${CONFIG}")"
[ "${TREE_FOUND}" = "${FOREIGN_GIT_TREE}" ]

echo OK
