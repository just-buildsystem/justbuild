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

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly WRKDIR="${PWD}/work"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/out"
readonly ORIG_SRC="${TEST_TMPDIR}/orig"

# Generate an archive and make the content known to CAS
# =====================================================

mkdir -p "${ORIG_SRC}"
cd "${ORIG_SRC}"
mkdir -p src/some/deep/directory
echo 'some data values' > src/some/deep/directory/data.txt
cat > src/TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "deps": ["some/deep/directory/data.txt"]
  , "cmds": ["cat some/deep/directory/data.txt | tr a-z A-Z > out.txt"]
  }
}
EOF
tar cvf source.tar src
SOURCE_HASH=$("${JUST}" add-to-cas --local-build-root "${LBR}" source.tar)
rm -rf source.tar src

echo "Upstream hash is ${SOURCE_HASH}"

# Use that archive
# ================

mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "archive"
      , "content": "${SOURCE_HASH}"
      , "fetch": "http://nonexistent.upstream.example.com/source.tar"
      , "subdir": "src"
      }
    }
  }
}
EOF
cat repos.json

CONF=$("${JUST_MR}" --norc --local-build-root "${LBR}" setup)
cat "${CONF}"
echo
GIT_ROOT=$(jq -r '.repositories.""."workspace_root" | .[2]' "${CONF}")
echo "Git root is ${GIT_ROOT}"
echo

"${JUST}" install -C "${CONF}" --local-build-root "${LBR}" \
          -L '["env", "PATH='"${PATH}"'"]' -o "${OUT}" 2>&1
# sanity check
grep VALUES "${OUT}/out.txt"


# Clean up build root: CAS/cache fully, rotate repo cache
# =======================================================

# Before rotation the git root should still exist
[ -e "${GIT_ROOT}" ]
"${JUST_MR}" --local-build-root "${LBR}" --just "${JUST}" gc-repo 2>&1
# After gc rotation, the original git root should no longer exist
[ -e "${GIT_ROOT}" ] && exit 1 || :

"${JUST_MR}" --local-build-root "${LBR}" --just "${JUST}" gc 2>&1
"${JUST_MR}" --local-build-root "${LBR}" --just "${JUST}" gc 2>&1


# Building should nevertheless succeed, due to the old repo generation
# =====================================================================

"${JUST_MR}" --just "${JUST}" --local-build-root "${LBR}" \
          -L '["env", "PATH='"${PATH}"'"]' install -o "${OUT}" 2>&1
# sanity check
grep VALUES "${OUT}/out.txt"
rm -f "${OUT}/out.txt"


# Repeat the whole procedure several times
# ========================================

for _ in `seq 1 5`
do

CONF=$("${JUST_MR}" --norc --local-build-root "${LBR}" setup)
GIT_ROOT=$(jq -r '.repositories.""."workspace_root" | .[2]' "${CONF}")
echo "Git root is ${GIT_ROOT}"

[ -e "${GIT_ROOT}" ]
"${JUST_MR}" --local-build-root "${LBR}" --just "${JUST}" gc-repo 2>&1
[ -e "${GIT_ROOT}" ] && exit 1 || :

"${JUST_MR}" --local-build-root "${LBR}" --just "${JUST}" gc 2>&1
"${JUST_MR}" --local-build-root "${LBR}" --just "${JUST}" gc 2>&1
"${JUST_MR}" --just "${JUST}" --local-build-root "${LBR}" \
          -L '["env", "PATH='"${PATH}"'"]' install -o "${OUT}" 2>&1
grep VALUES "${OUT}/out.txt"
rm -f "${OUT}/out.txt"

done

# Finally demonstrate that the root was not taken from anything but the cache
# ===========================================================================

"${JUST_MR}" --local-build-root "${LBR}" --just "${JUST}" gc-repo 2>&1
"${JUST_MR}" --local-build-root "${LBR}" --just "${JUST}" gc-repo 2>&1

# after full rotation of the repository, the root should be lost
"${JUST_MR}" --norc --local-build-root "${LBR}" -f "${OUT}/log" \
             setup 2>&1 && exit 1 || :

# sanity check: the error message should mention the fetch host
grep nonexistent.upstream.example.com "${OUT}/log"

echo OK
