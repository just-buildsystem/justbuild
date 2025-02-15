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

readonly ROOT="${PWD}"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly CLIENT_A="${TEST_TMPDIR}/local-build-root-A"
readonly CLIENT_B="${TEST_TMPDIR}/local-build-root-B"
readonly CLIENT_SYMLINK_CHECK="${TEST_TMPDIR}/local-build-root-symlink-check"
readonly RESOLVE_SPECIAL_CHECK_A="${TEST_TMPDIR}/local-build-root-resolve-special-A"
readonly RESOLVE_SPECIAL_CHECK_B="${TEST_TMPDIR}/local-build-root-resolve-special-B"
readonly RESOLVE_SPECIAL_CHECK_C="${TEST_TMPDIR}/local-build-root-resolve-special-C"
readonly RESOLVE_SPECIAL_CHECK_D="${TEST_TMPDIR}/local-build-root-resolve-special-D"
readonly RESOLVE_SPECIAL_CHECK_E="${TEST_TMPDIR}/local-build-root-resolve-special-E"
readonly TOOLS_DIR="${TEST_TMPDIR}/tools"
readonly OUT_A="${TEST_TMPDIR}/out/client-A"
readonly OUT_B="${TEST_TMPDIR}/out/client-B"
readonly ARCHIVES_DIR="${TEST_TMPDIR}/archives"
readonly LINK_DIR="${TEST_TMPDIR}/add-symlinks-here"


REMOTE_EXECUTION_ARGS="-r ${REMOTE_EXECUTION_ADDRESS}"

# "checkout" of a repository
WORK="${ROOT}/work"
mkdir work
cat > work/TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "cmds":
    ["echo -n 'Hello ' > out.txt", "cat name >> out.txt", "echo '!' >> out.txt"]
  , "outs": ["out.txt"]
  , "deps": ["name"]
  }
}
EOF
echo -n World > work/name

# There is also an archive with the sources
mkdir -p "${ARCHIVES_DIR}"
tar cvf "${ARCHIVES_DIR}/src.tar" work
echo

# Mock vcs tool that should not be called
mkdir -p "${TOOLS_DIR}"
VCS="${TOOLS_DIR}/mock-vcs"
cat > "${VCS}" <<'EOF'
#!/bin/sh
echo "It should not be necessary to call the foreign VCS"
exit 1
EOF
chmod 755 "${VCS}"

# Preliminaries: verifying link hashing and --follow-symlinks

mkdir -p "${LINK_DIR}"
ln -s "${WORK}" "${LINK_DIR}/dir"

dir_link_hash=$("${JUST}" add-to-cas --local-build-root "${CLIENT_SYMLINK_CHECK}" "${LINK_DIR}/dir")
dir_link="$("${JUST}" install-cas --local-build-root "${CLIENT_SYMLINK_CHECK}" "$dir_link_hash")"
[ "$dir_link" = "${WORK}" ]

dir_follow=$("${JUST}" add-to-cas --local-build-root "${CLIENT_SYMLINK_CHECK}" --follow-symlinks "${LINK_DIR}/dir")
dir_hash=$("${JUST}" add-to-cas --local-build-root "${CLIENT_SYMLINK_CHECK}" --follow-symlinks "${WORK}")

[ "$dir_follow" = "$dir_hash" ]


# Use case: "git tree" repositories and avoiding an additional
#           call to the foreign version-control system
cd ${ROOT}
tree=$("${JUST}" add-to-cas --local-build-root "${CLIENT_A}" work)
rm -rf work

mkdir "${ROOT}/build-A"
cd "${ROOT}/build-A"
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "git tree"
      , "id": "$tree"
      , "cmd": ["${VCS}", "checkout", "r1234"]
      }
    }
  }
}
EOF
cat repos.json
echo

"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${CLIENT_A}" \
             -L '["env", "PATH='"${PATH}"'"]' \
             install -o "${OUT_A}" 2>&1

echo
grep World "${OUT_A}/out.txt"
echo

# Use case: Backing up a distfile to the remote-execution service
cd "${ROOT}"
archive=$("${JUST}" add-to-cas --local-build-root "${CLIENT_A}" \
                    ${REMOTE_EXECUTION_ARGS} "${ARCHIVES_DIR}/src.tar")
rm -f "${ARCHIVES_DIR}/src.tar"

mkdir -p "${ROOT}/build-B"
cd "${ROOT}/build-B"
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "archive"
      , "content": "$archive"
      , "subdir": "work"
      , "fetch": "http://example.org/src.tar"
      }
    }
  }
}
EOF
cat repos.json
echo

"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${CLIENT_B}" \
             ${REMOTE_EXECUTION_ARGS} \
             install -o "${OUT_B}" 2>&1

echo
grep World "${OUT_B}/out.txt"
echo

# Extension: Using option --resolve-special
NUM=5
TREE_ROOT="${ROOT}/tree"
mkdir -p "${TREE_ROOT}"
(
  # create dirs and links structure
  DIR="${TREE_ROOT}"
  for i in `seq 1 $NUM`; do
    TO="."
    for j in `seq $i $NUM`; do
      TO="${TO}/$j"
      # multiple links pointing to same place
      for k in `seq 1 $NUM`; do
        x=$(($j + ($k - 1) * $NUM))
        ln -s "${TO}" "${DIR}/link$x"
      done
    done
    DIR="${DIR}/$i"
    mkdir "${DIR}"
  done
  # create 1 single file
  echo content > "${DIR}/data"
)

# whether symlinks ignored, resolved inside tree or resolved completely:
# expect 6 trees and 2 blobs (1 for data, 1 for exec properties description)
"${JUST}" add-to-cas --local-build-root "${RESOLVE_SPECIAL_CHECK_A}" --resolve-special=ignore "${TREE_ROOT}" 2>&1
[ "$(find "${RESOLVE_SPECIAL_CHECK_A}/protocol-dependent/generation-0" -type f | grep cast | wc -l)" = $(($NUM + 1)) ]
[ "$(find "${RESOLVE_SPECIAL_CHECK_A}/protocol-dependent/generation-0" -type f | grep casf | wc -l)" = 2 ]

"${JUST}" add-to-cas --local-build-root "${RESOLVE_SPECIAL_CHECK_B}" --resolve-special=tree-all "${TREE_ROOT}" 2>&1
[ "$(find "${RESOLVE_SPECIAL_CHECK_B}/protocol-dependent/generation-0" -type f | grep cast | wc -l)" = $(($NUM + 1)) ]
[ "$(find "${RESOLVE_SPECIAL_CHECK_B}/protocol-dependent/generation-0" -type f | grep casf | wc -l)" = 2 ]

"${JUST}" add-to-cas --local-build-root "${RESOLVE_SPECIAL_CHECK_C}" --resolve-special=all "${TREE_ROOT}" 2>&1
[ "$(find "${RESOLVE_SPECIAL_CHECK_C}/protocol-dependent/generation-0" -type f | grep cast | wc -l)" = $(($NUM + 1)) ]
[ "$(find "${RESOLVE_SPECIAL_CHECK_C}/protocol-dependent/generation-0" -type f | grep casf | wc -l)" = 2 ]

(
  # create upward symlinks
  DIR="${TREE_ROOT}"
  for i in `seq 1 $NUM`; do
    ln -s "../link" "${DIR}/link"
    DIR="${DIR}/$i"
  done
  # link the outmost entry to the data
  ln -s "${DIR}/data" "${ROOT}/link"
)

# upward symlinks cannot be resolved inside tree...
"${JUST}" add-to-cas --local-build-root "${RESOLVE_SPECIAL_CHECK_D}" \
                     --resolve-special=tree-all "${TREE_ROOT}" 2>&1 && echo expected to fail || :
echo failed as expected
echo
# ...but they can get resolved outside of it; expect same number of CAS entries
"${JUST}" add-to-cas --local-build-root "${RESOLVE_SPECIAL_CHECK_E}" --resolve-special=all "${TREE_ROOT}" 2>&1
[ "$(find "${RESOLVE_SPECIAL_CHECK_E}/protocol-dependent/generation-0" -type f | grep cast | wc -l)" = $(($NUM + 1)) ]
[ "$(find "${RESOLVE_SPECIAL_CHECK_E}/protocol-dependent/generation-0" -type f | grep casf | wc -l)" = 2 ]

echo OK
