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

###
# Git repositories treat some filenames, such as .git, .gitignore etc., as
# magic names. We should not have such restrictions in handling blobs and trees.
#
# This test checks that trees can be added to CAS as they are, without
# ignoring/skipping any files or folders that might have special meaning to Git.
##

set -eu

ROOT=$(pwd)
JUST="${ROOT}/bin/tool-under-test"
JUST_MR="${ROOT}/bin/mr-tool-under-test"
BUILD_ROOT_A="${TEST_TMPDIR}/build-root-A"
BUILD_ROOT_B="${TEST_TMPDIR}/build-root-B"
BUILD_ROOT_C="${TEST_TMPDIR}/build-root-C"

# create a Git repo, including a .gitignore file
mkdir -p repo
echo simple file > repo/foo.txt
(cd repo && ln -s foo symlink)
mkdir -p repo/foo/bar
echo another file > repo/foo/bar/data.txt
(
  cd repo
  echo data.txt > foo/bar/.gitignore
  git init
  git config user.name 'N.O.Body'
  git config user.email 'nobody@example.org'
  git add -f . 2>&1
  git commit -m "initial commit"
)

find repo ! -name . -printf "%P\n"
tar cf repo.tar repo

# Add repo directory to CAS and print tree id as we see it (with all entries)
echo
TREE=$("${JUST}" add-to-cas --local-build-root "${BUILD_ROOT_A}" repo)
echo "Source tree is ${TREE}"
echo

# Generate archive and verify it has the correct content
"${JUST}" install-cas --local-build-root "${BUILD_ROOT_A}" \
          --archive -o repo.tar "${TREE}::t" 2>&1
mkdir repo.unpacked
(cd repo.unpacked && tar xvf ../repo.tar 2>&1)
diff -ruN repo repo.unpacked

# clean up no longer needed directories
rm -rf repo.unpacked

# On a new build root, add the archive
ARCHIVE=$("${JUST}" add-to-cas --local-build-root "${BUILD_ROOT_B}" repo.tar)
echo
echo Archive has content "${ARCHIVE}"
echo

# Verify that we can create the original tree from that archive
mkdir work
cd work
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "archive"
      , "content": "${ARCHIVE}"
      , "fetch": "https://example.org/v1.2.3.tar"
      }
    , "target_root": "targets"
    }
  , "targets": {"repository": {"type": "file", "path": "."}}
  }
}
EOF
cat repos.json
echo
cat > TARGETS <<'EOF'
{"": {"type": "install", "dirs": [[["TREE", null, "."], "."]]}}
EOF
CONF="$("${JUST_MR}" --norc --local-build-root "${BUILD_ROOT_B}" setup)"
echo
echo configuration $CONF
cat $CONF
echo
RECONSTRUCTED_TREE=$(jq -r '.repositories."".workspace_root[1]' "${CONF}")
[ "${RECONSTRUCTED_TREE}" = "${TREE}" ]

# Build to get the tree unconditionally known to the local build root
"${JUST}" build --local-build-root "${BUILD_ROOT_B}" -C "${CONF}" 2>&1

# - installing the tree as archive should give the same content
"${JUST}" install-cas --local-build-root "${BUILD_ROOT_B}" \
          -o "${ROOT}/reconstructed.tar" --archive "${TREE}::t" 2>&1
cmp "${ROOT}/repo.tar" "${ROOT}/reconstructed.tar" 2>&1

# - dumping the tree as archive to stdout also gives the same archive
"${JUST}" install-cas --local-build-root "${BUILD_ROOT_B}" \
          --archive "${TREE}::t" > "${ROOT}/fromstdout.tar"
cmp "${ROOT}/repo.tar" "${ROOT}/fromstdout.tar" 2>&1

# - hashing the archive again gives the original value
ARCHIVE_NEWLY_HASHED=$("${JUST}" add-to-cas --local-build-root "${BUILD_ROOT_C}" "${ROOT}/fromstdout.tar")
[ "${ARCHIVE_NEWLY_HASHED}" = "${ARCHIVE}" ]

echo OK
