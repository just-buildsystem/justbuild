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
readonly LBR_MR="${TEST_TMPDIR}/local-build-root-mr"
readonly TOOLS_DIR="${TEST_TMPDIR}/tools"
readonly OUT="${TEST_TMPDIR}/out"
readonly JUST_MR_ARGS="--norc --just ${JUST} --local-build-root ${LBR_MR}"
BUILD_ARGS="--local-build-root ${LBR}"
if [ -n "${COMPATIBLE:-}" ]; then
  BUILD_ARGS="$BUILD_ARGS --compatible"
fi

# Have tools in the "outside environment" to later demonstrate
# that the action was _not_ rerun.
mkdir -p "${TOOLS_DIR}"
cat > "${TOOLS_DIR}/tree" <<'EOF'
#!/bin/sh
mkdir -p $1/hello/world/tree
echo Hello World > $1/hello/world/tree/hello.txt
echo -n World > $1/hello/world/tree/name.txt
EOF
chmod 755 "${TOOLS_DIR}/tree"

mkdir work
cd work

touch WORKSPACE
cat > TARGETS <<'EOF'
{ "tree":
  { "type": "generic"
  , "arguments_config": ["ENV"]
  , "out_dirs": ["out"]
  , "cmds": ["${TOOLS}/tree out"]
  , "env": {"type": "var", "name": "ENV"}
  }
, "":
  { "type": "export"
  , "flexible_config": ["ENV"]
  , "target": "tree"
  }
}
EOF

cat > repos.json <<'EOF'
{ "main": ""
, "repositories":
  { "":
    {"repository": {"type": "file", "path": ".", "pragma": {"to_git": true}}}
  }
}
EOF

cat repos.json
cat TARGETS

# Build to fill the cache
"${JUST_MR}" ${JUST_MR_ARGS} build ${BUILD_ARGS} \
          -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' 2>&1

# Demonstrate that from now on, we don't build anything any more
rm -rf "${TOOLS_DIR}"

# Demonstrate that we can build the export target without any cached actions
rm -rf ${LBR}/protocol-dependent/generation-*/*/ac

# collect garbage
"${JUST_MR}" ${JUST_MR_ARGS} gc --local-build-root ${LBR} 2>&1

# Use the export
"${JUST_MR}" ${JUST_MR_ARGS} build ${BUILD_ARGS} \
          -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' 2>&1

# collect garbage again
"${JUST_MR}" ${JUST_MR_ARGS} gc --local-build-root ${LBR} 2>&1

# Verify that the export target is fully in cache
"${JUST_MR}" ${JUST_MR_ARGS} install ${BUILD_ARGS} -o "${OUT}" \
          -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' 2>&1
ls -R "${OUT}"
test -f "${OUT}/out/hello/world/tree/hello.txt"
test -f "${OUT}/out/hello/world/tree/name.txt"
test "$(cat "${OUT}/out/hello/world/tree/name.txt")" = "World"

# check if all files in generation 0 have been linked and not copied
readonly NON_LINKED_FILES=$(find ${LBR}/protocol-dependent/generation-0 -type f -exec sh -c 'test $(stat -c %h {}) != 2' \; -print)
echo
echo "Files with link count!=2:"
echo "${NON_LINKED_FILES:-"<none>"}"
echo
test -z "${NON_LINKED_FILES}"

echo OK
