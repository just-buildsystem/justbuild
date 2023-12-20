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

###
# Check that a build will attempt to get the target cache value from just serve
# during analysis of an export target in the case of a local target cache miss.
##

set -eu

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"

readonly TOOLS_DIR="${TEST_TMPDIR}/tools"
readonly OUT="${TEST_TMPDIR}/out"

readonly DISPATCH_FILE="${TEST_TMPDIR}/dispatch.json"

cat > "${DISPATCH_FILE}" <<EOF
[[{"runner": "node-name"}, "127.0.0.1:1234"]]
EOF

readonly REMOTE_PROPERTIES="--remote-execution-property foo:bar"
readonly DISPATCH="--endpoint-configuration ${DISPATCH_FILE}"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

# Have tools in the "outside environment"
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

# Build to fill the target cache of the serve endpoint
CONF=$("${JUST_MR}" --norc --local-build-root "${SERVE_LBR}" setup)

echo "generated conf":
cat "${CONF}"
echo

"${JUST}" build                       \
    --local-build-root "${SERVE_LBR}" \
    -C "${CONF}"                      \
    -r "${REMOTE_EXECUTION_ADDRESS}"  \
    ${COMPAT}                         \
    ${REMOTE_PROPERTIES}              \
    ${DISPATCH}                       \
    -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' 2>&1

ls -R "${SERVE_LBR}"

# Demonstrate that from now on, we don't build anything any more
rm -rf "${TOOLS_DIR}"

# Setup for a build in a new build root
CONF=$("${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" setup)

echo "generated conf":
cat "${CONF}"
echo

# Demonstrate that we can analyse, but not build locally
"${JUST}" analyse                     \
    --local-build-root "${LBR}"       \
    -C "${CONF}"                      \
    -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' 2>&1

"${JUST}" build                       \
    --local-build-root "${LBR}"       \
    -C "${CONF}"                      \
    -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' 2>&1 && echo "this should fail" && exit 1
echo "failed as expected"

# Demonstrate we cannot build with a clean remote CAS
"${JUST}" gc --local-build-root ${REMOTE_LBR} 2>&1
"${JUST}" gc --local-build-root ${REMOTE_LBR} 2>&1

"${JUST}" build                       \
    --local-build-root "${LBR}"       \
    -C "${CONF}"                      \
    -r "${REMOTE_EXECUTION_ADDRESS}"  \
    ${COMPAT}                         \
    ${REMOTE_PROPERTIES}              \
    ${DISPATCH}                       \
    -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' 2>&1 && echo "this should fail" && exit 1q
echo "failed as expected"

# Demonstrate that we can build if serve endpoint provides the target cache value
"${JUST}" build                       \
    --local-build-root "${LBR}"       \
    -C "${CONF}"                      \
    --remote-serve-address "${SERVE}" \
    -r "${REMOTE_EXECUTION_ADDRESS}"  \
    ${COMPAT}                         \
    ${REMOTE_PROPERTIES}              \
    ${DISPATCH}                       \
    --log-limit 5                     \
    -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' 2>&1

# Verify that the export target is fully in cache
"${JUST}" install                     \
    --local-build-root "${LBR}"       \
    -C "${CONF}"                      \
    --remote-serve-address "${SERVE}" \
    -r "${REMOTE_EXECUTION_ADDRESS}"  \
    ${COMPAT}                         \
    ${REMOTE_PROPERTIES}              \
    ${DISPATCH}                       \
    --log-limit 5                     \
    -o "${OUT}"                       \
    -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' 2>&1
ls -R "${OUT}"
test -f "${OUT}/out/hello/world/tree/hello.txt"
test -f "${OUT}/out/hello/world/tree/name.txt"
test "$(cat "${OUT}/out/hello/world/tree/name.txt")" = "World"

echo OK
