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
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly JUST_ARGS="--local-build-root ${LBR}"

readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR_MR="${TEST_TMPDIR}/local-build-root-mr"
readonly JUST_MR_ARGS="--norc --local-build-root ${LBR_MR} --just ${JUST}"

STORAGE="${LBR}/protocol-dependent/generation-0"
COMPATIBLE_ARGS=""
if [ -n "${COMPATIBLE:-}" ]; then
  COMPATIBLE_ARGS="--compatible"
  STORAGE="${STORAGE}/compatible-sha256"
else
  STORAGE="${STORAGE}/git-sha1"
fi

readonly TOOLS_DIR="${TEST_TMPDIR}/tools"

mkdir -p "${TOOLS_DIR}"
cat > "${TOOLS_DIR}/make_large.sh" <<'EOF'
#!/bin/sh
readonly Mb=$((1024 * 1024))
readonly COUNT=$((${1} * ${Mb} / 8))
for _ in `seq 1 ${COUNT}`
do
  echo 01234567
done
EOF
chmod 755 "${TOOLS_DIR}/make_large.sh"

mkdir work && cd work

touch ROOT
cat > repos.json <<'EOF'
{ "repositories":
  { "":
    {"repository": {"type": "file", "path": ".", "pragma": {"to_git": true}}}
  }
}
EOF

cat > TARGETS <<'EOF'
{ "large":
  { "type": "generic"
  , "arguments_config": ["ENV"]
  , "outs": ["out.txt"]
  , "cmds": ["${TOOLS}/make_large.sh 5 > out.txt"]
  , "env": {"type": "var", "name": "ENV"}
  }
, "large_export":
  { "type": "export"
  , "flexible_config": ["ENV"]
  , "target": "large"
  }
, "main":
  { "type": "generic"
  , "arguments_config": ["ENV"]
  , "outs": ["dummy.txt"]
  , "cmds": ["cat out.txt out.txt > dummy.txt"]
  , "env": {"type": "var", "name": "ENV"}
  , "deps": ["large_export"]
  }
, "": {"type": "install", "dirs": [["large", "out"], ["main", "out"]]}
}
EOF

cat TARGETS

readonly GRPC_THRESHOLD_MB=4
get_large_files_count()
{
  find "${STORAGE}" -size +${GRPC_THRESHOLD_MB}M | wc -l
}

get_lbr_storage_size()
{
    du -hs --block-size=1 "${STORAGE}" | cut -f 1
}

print_storage_statistics()
{
  local size="$(get_lbr_storage_size)"
  local count="$(get_large_files_count)"

  echo "SIZE STORAGE: ${size}"
  echo "COUNT OF FILES LARGER THAN ${GRPC_THRESHOLD_MB} MB IS: ${count}"
}

# Build to fill the cache
"${JUST_MR}" ${JUST_MR_ARGS} build ${JUST_ARGS} ${COMPATIBLE_ARGS} \
          -L '["env", "PATH='"${PATH}"'"]' \
          -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' 2>&1

# Demonstrate that from now on, we don't build anything any more
rm -rf "${TOOLS_DIR}"

readonly SIZE_BEFORE="$(get_lbr_storage_size)"
readonly COUNT_BEFORE="$(get_large_files_count)"
echo 'BEFORE COMPACTIFICATION:'
print_storage_statistics
[ ${COUNT_BEFORE} -gt 0 ] && [ ${COUNT_BEFORE} -le 3 ]

# Run compactification of the last generation. No large files must remain.
"${JUST}" gc ${JUST_ARGS} --no-rotate

readonly COMPACTIFIED_SIZE=$(get_lbr_storage_size)
readonly COUNT_COMPACTIFIED=$(get_large_files_count)

echo 'AFTER COMPACTIFICATION:'
print_storage_statistics
[ ${COMPACTIFIED_SIZE} -le ${SIZE_BEFORE} ]
[ ${COUNT_COMPACTIFIED} -eq 0 ]

# Build one more time to ensure that for fully cached builds nothing except export targets gets reconstructed
"${JUST_MR}" ${JUST_MR_ARGS} build ${JUST_ARGS} ${COMPATIBLE_ARGS} \
          -L '["env", "PATH='"${PATH}"'"]' \
          -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' 2>&1

readonly RECONSTRUCTED_SIZE=$(get_lbr_storage_size)
readonly RECONSTRUCTED_COUNT=$(get_large_files_count)

echo 'AFTER RECONSTRUCTION:'
print_storage_statistics
# Reconstruction of export targets is allowed:
[ ${RECONSTRUCTED_COUNT} -le 1 ] 

readonly EXPORT_PATH=$(find "${STORAGE}" -size +${GRPC_THRESHOLD_MB}M)
readonly EXPORT_SIZE=$(du -hs --block-size=1 "${EXPORT_PATH}" | cut -f 1)
readonly EXPECTED_SIZE=$((${COMPACTIFIED_SIZE}+${EXPORT_SIZE}))
echo "EXPECTED SIZE IS ${EXPECTED_SIZE}"
[ ${RECONSTRUCTED_SIZE} -le ${EXPECTED_SIZE} ]

# Rotation and building again should not reconstruct anything except export targets.
"${JUST}" gc ${JUST_ARGS}
"${JUST_MR}" ${JUST_MR_ARGS} build ${JUST_ARGS} ${COMPATIBLE_ARGS} \
          -L '["env", "PATH='"${PATH}"'"]' \
          -D '{"ENV": {"TOOLS": "'${TOOLS_DIR}'"}}' 2>&1

readonly ROTATED_BUILD_SIZE=$(get_lbr_storage_size)
readonly ROTATED_BUILD_COUNT=$(get_large_files_count)

echo 'AFTER ROTATION AND BUILD:'
print_storage_statistics
[ ${ROTATED_BUILD_SIZE} -le ${EXPECTED_SIZE} ]
[ ${ROTATED_BUILD_COUNT} -le 1 ]

# Calculate the size difference. 
# The resulting size after rotation must not exceed the size of compactification + the size of the export target:
readonly SIZE_DIFFERENCE=$((${ROTATED_BUILD_SIZE}-${COMPACTIFIED_SIZE}-${EXPORT_SIZE}))
echo "SIZE DIFFERENCE IS ${SIZE_DIFFERENCE}"
[ ${SIZE_DIFFERENCE} -le 0 ]
echo OK
