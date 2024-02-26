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
readonly DISTDIR="${TEST_TMPDIR}/distfiles"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/out"

mkdir -p "${DISTDIR}"

echo This-is-the-content > "${DISTDIR}/data.txt"
HASH=$(git hash-object "${DISTDIR}/data.txt")

# Setup sample repository config
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "data":
    { "repository":
      { "type": "foreign file"
      , "content": "${HASH}"
      , "fetch": "http://non-existent.example.org/data.txt"
      , "name": "content.txt"
      }
    }
  , "local": {"repository": {"type": "file", "path": "."}}
  , "": {"repository": "data", "target_root": "local"}
  }
}
EOF
echo "Repository configuration:"
cat repos.json

cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["cat content.txt | tr a-z A-Z > out.txt"]
  , "deps": ["content.txt"]
  }
}
EOF

# Build to verify that foreign-file roots work; this will also make just-mr
# aware of the file.

mkdir -p "${OUT}"

"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
             -L '["env", "PATH='"${PATH}"'"]' \
             --distdir "${DISTDIR}" \
             install -o "${OUT}" 2>&1

grep THIS-IS-THE-CONTENT "${OUT}/out.txt"

# Remove distdir content
rm -rf "${DISTDIR}"
mkdir -p "${DISTDIR}"

# Ask just-mr to fetch to the empty distdir
"${JUST_MR}" --norc --local-build-root "${LBR}" fetch -o "${DISTDIR}" 2>&1

test -f "${DISTDIR}/data.txt"
NEW_HASH=$(git hash-object "${DISTDIR}/data.txt")

test "${HASH}" = "${NEW_HASH}"

# Verify that the root is properly cached, i.e., that we can build again
# after cleaning out cache and CAS, and without using the distdir

"${JUST}" gc --local-build-root "${LBR}" 2>&1
"${JUST}" gc --local-build-root "${LBR}" 2>&1
rm -f "${OUT}/out.txt"
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
             -L '["env", "PATH='"${PATH}"'"]' \
             install -o "${OUT}" 2>&1

grep THIS-IS-THE-CONTENT "${OUT}/out.txt"

echo OK
