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

set -e

ROOT=$(pwd)
TOOL=$(realpath ./bin/tool-under-test)
BUILDROOT="${TEST_TMPDIR}/build-root"

mkdir src
touch src/ROOT
cat > src/TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["hello.txt"]
  , "cmds": ["echo Hello World > hello.txt"]
  }
, "symlink":
  { "type": "generic"
  , "outs": ["hello.txt", "content.txt"]
  , "cmds": ["echo Hello World > content.txt", "ln -s content.txt hello.txt"]
  }
}
EOF
SRCDIR=$(realpath src)

mkdir out
echo 'Original' > out/unrelated.txt
ln out/unrelated.txt out/hello.txt
OUTDIR=$(realpath out)
ls -al "${OUTDIR}"
echo

# Verify non-interference of install
cd "${SRCDIR}"
"${TOOL}" install --local-build-root "${BUILDROOT}" -o "${OUTDIR}" 2>&1

echo
ls -al "${OUTDIR}"
cd "${OUTDIR}"
grep World hello.txt
grep Original unrelated.txt

# Verify non-interference of install
cd "${SRCDIR}"
"${TOOL}" build --local-build-root "${BUILDROOT}" \
          --dump-artifacts "${OUTDIR}/artifacts.json" 2>&1
echo
ID=$(jq -rM '."hello.txt".id' "${OUTDIR}/artifacts.json")
ln "${OUTDIR}/unrelated.txt" "${OUTDIR}/${ID}"
ls -al "${OUTDIR}"
echo

"${TOOL}" install-cas --local-build-root "${BUILDROOT}" \
          -o "${OUTDIR}" "${ID}" 2>&1
echo
ls -al "${OUTDIR}"
cd "${OUTDIR}"
grep World "${ID}"
grep Original unrelated.txt

# Verify non-interference of install symlinks (overwrite existing file)
cd "${SRCDIR}"
"${TOOL}" install --local-build-root "${BUILDROOT}" -o "${OUTDIR}" symlink 2>&1

echo
ls -al "${OUTDIR}"
cd "${OUTDIR}"
grep World hello.txt
grep Original unrelated.txt
[ "$(realpath --relative-to=$(pwd) hello.txt)" = "content.txt" ]

# Verify non-interference of install symlinks (overwrite existing symlink)
rm -f ${OUTDIR}/hello.txt
ln -s /noexistent ${OUTDIR}/hello.txt
cd "${SRCDIR}"
"${TOOL}" install --local-build-root "${BUILDROOT}" -o "${OUTDIR}" symlink 2>&1

echo
ls -al "${OUTDIR}"
cd "${OUTDIR}"
grep World hello.txt
grep Original unrelated.txt
[ "$(realpath --relative-to=$(pwd) hello.txt)" = "content.txt" ]

echo OK
