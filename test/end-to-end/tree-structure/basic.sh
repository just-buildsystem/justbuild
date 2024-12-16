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

set -e

readonly ROOT="$(pwd)"
readonly LBRDIR="$TMPDIR/local-build-root"
readonly JUST="${ROOT}/bin/tool-under-test"
readonly OUT="${ROOT}/out"
mkdir -p "${OUT}"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

readonly TARGET_ROOT="${ROOT}/target_root"
mkdir -p "${TARGET_ROOT}"
cd "${TARGET_ROOT}"
cat > "TARGETS.install_structure" << 'EOF'
{ "": {"type": "export", "target": "test"}
  , "test":
  { "type": "install"
  , "deps": [["TREE", null, "test_dir"]]
  }
}
EOF

echo "TARGETS.install_structure":
cat "TARGETS.install_structure"
echo

cat > "TARGETS.ls_structure" << 'EOF'
{ "": {"type": "export", "target": "generate"}
  , "generate":
  { "type": "generic"
  , "deps": [["TREE", null, "test_dir"]]
  , "outs": ["result.txt"]
  , "cmds": ["ls -R test_dir > result.txt"]
  }
}
EOF

echo "TARGETS.ls_structure":
cat "TARGETS.ls_structure"
echo

echo "Backing up the target_root to git:"
git init 2>&1
git branch -m stable-1.0 2>&1
git config user.email "nobody@example.org" 2>&1
git config user.name "Nobody" 2>&1
git add . 2>&1
git commit -m "TR: initial commit" 2>&1
readonly TR_GIT_TREE=$(git log -n 1 --format="%T")

# Create source directory and init git:
readonly SRC_DIR="${ROOT}/src"
mkdir -p "${SRC_DIR}"
cd "${SRC_DIR}"
git init 2>&1
git branch -m stable-1.0 2>&1
git config user.email "nobody@example.org" 2>&1
git config user.name "Nobody" 2>&1

readonly NESTED_DIR_2="${SRC_DIR}/test_dir/nested_dir/nested_dir_2"
readonly FILE_TO_BE_CHANGED="${NESTED_DIR_2}/file"
mkdir -p "${NESTED_DIR_2}"
echo "initial content" > "${FILE_TO_BE_CHANGED}"

# Obtain the initial git tree:
git add . 2>&1
git commit -m "Initial commit" 2>&1
readonly INITIAL_GIT_TREE=$(git log -n 1 --format="%T")
echo "tree hash: ${INITIAL_GIT_TREE}"

# Obtain the updated git tree:
echo "updated content" > "${FILE_TO_BE_CHANGED}"
git add . 2>&1
git commit -m "Update file" 2>&1
readonly UPDATED_GIT_TREE=$(git log -n 1 --format="%T")
echo "tree hash: ${UPDATED_GIT_TREE}"

readonly MAIN_ROOT="${ROOT}/main"
mkdir -p "${MAIN_ROOT}"
cd "${MAIN_ROOT}"

echo "Creating repo-config at ${MAIN_ROOT}:"
cat > repo-config.json << EOF
{ "repositories": 
  { "initial_sources": 
    { "workspace_root": ["git tree", "${INITIAL_GIT_TREE}", "${SRC_DIR}/.git"]
    , "target_root": ["git tree", "${TR_GIT_TREE}", "${TARGET_ROOT}/.git"]
    , "target_file_name": "TARGETS.install_structure"
    }
  , "tree_structure_1": 
    { "workspace_root": ["tree structure", "initial_sources"]
    , "target_root": ["git tree", "${TR_GIT_TREE}", "${TARGET_ROOT}/.git"]
    , "target_file_name": "TARGETS.ls_structure"
    }
  , "updated_sources": 
    { "workspace_root": ["git tree", "${UPDATED_GIT_TREE}", "${SRC_DIR}/.git"]
    , "target_root": ["git tree", "${TR_GIT_TREE}", "${TARGET_ROOT}/.git"]
    , "target_file_name": "TARGETS.install_structure"
    }
  , "tree_structure_2":
    { "workspace_root": ["tree structure", "updated_sources"]
    , "target_root": ["git tree", "${TR_GIT_TREE}", "${TARGET_ROOT}/.git"]
    , "target_file_name": "TARGETS.ls_structure"
    }
  }
}
EOF
cat repo-config.json
echo

# initial_sources and updated_sources have obviously different workspace roots:
# the files have different content, so the respective trees are different
# as well.
# BUT tree_structure_1 and tree_structure_2 produce the same result, since
# they rely on the "tree structure" of the sources, which are the same.
# Even so tree_structure_2 doesn't get built directly, it still can be taken
# from cache, since tree_structure_1 is keyed in the same way as 
# tree_structure_2 is.

echo "Building tree_structure_1 (expected a new cache entry):"
("${JUST}" install -L '["env", "PATH='"${PATH}"'"]' "${COMPAT}" \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    --main tree_structure_1 --log-limit 4 -o "${OUT}/tree_structure_1" 2>&1) > \
    "${OUT}/log"
echo

cat "${OUT}/log"
echo
grep 'Export target \["@","tree_structure_1","",""\] registered for caching' \
      "${OUT}/log"

echo "Building tree_structure_2 (expected to be taken from cache):"
("${JUST}" install -L '["env", "PATH='"${PATH}"'"]' "${COMPAT}" \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    --main tree_structure_2 --log-limit 4 -o "${OUT}/tree_structure_2" 2>&1) > \
    "${OUT}/log2"
echo

cat "${OUT}/log2"
echo
grep 'Export target \["@","tree_structure_2","",""\] taken from cache' \
      "${OUT}/log2"

echo OK
