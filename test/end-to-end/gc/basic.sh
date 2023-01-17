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
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly TOOLS_DIR="${TEST_TMPDIR}/tools"
readonly OUT="${TEST_TMPDIR}/out"

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
cat > "${TOOLS_DIR}/large" <<'EOF'
#!/bin/sh
for _ in `seq 1 10000`
do
  echo This is a really large file 1234567890 >> $1
done
EOF
chmod 755 "${TOOLS_DIR}/large"


touch WORKSPACE
cat > TARGETS <<'EOF'
{ "large":
  { "type": "generic"
  , "arguments_config": ["ENV"]
  , "outs": ["out.txt"]
  , "cmds": ["${TOOLS}/large out.txt"]
  , "env": {"type": "var", "name": "ENV"}
  }
, "tree":
  { "type": "generic"
  , "arguments_config": ["ENV"]
  , "out_dirs": ["out"]
  , "cmds": ["${TOOLS}/tree out"]
  , "env": {"type": "var", "name": "ENV"}
  }
, "": {"type": "install", "dirs": [["large", "out"], ["tree", "out"]]}
}
EOF

cat TARGETS

# Build to fill the cache
"${JUST}" build --local-build-root "${LBR}" \
          -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' 2>&1

# Demonstrate that from now on, we don't build anything any more
rm -rf "${TOOLS_DIR}"

# Verify the large file is in cache
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/out-large" \
          -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' large 2>&1
wc -c "${OUT}/out-large/out.txt"
test $(cat "${OUT}/out-large/out.txt" | wc -c) -gt 100000

# collect garbage
"${JUST}" gc --local-build-root "${LBR}" 2>&1

# Use the tree
"${JUST}" build --local-build-root "${LBR}" \
          -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' tree 2>&1

# collect garbage again
"${JUST}" gc --local-build-root "${LBR}" 2>&1

# Verify the build root is now small
tar cvf "${OUT}/root.tar" "${LBR}" 2>&1
wc -c "${OUT}/root.tar"
test $(cat "${OUT}/root.tar" | wc -c) -lt 100000

# Verify that the tree is fully in cache
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/out-tree" \
          -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' tree 2>&1
ls -R "${OUT}/out-tree"
test -f "${OUT}/out-tree/out/hello/world/tree/hello.txt"
test -f "${OUT}/out-tree/out/hello/world/tree/name.txt"
test "$(cat "${OUT}/out-tree/out/hello/world/tree/name.txt")" = "World"

echo OK
