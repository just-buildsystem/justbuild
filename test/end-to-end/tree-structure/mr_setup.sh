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
readonly JUST_MR="${ROOT}/bin/mr-tool-under-test"
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

# Create source directories that have the same structure, but different content
# of the file:
readonly SRC_DIR="${ROOT}/src"
readonly NESTED_DIR="test_dir/nested_dir/nested_dir_2"
mkdir -p "${SRC_DIR}/${NESTED_DIR}"
echo "initial content" > "${SRC_DIR}/${NESTED_DIR}/file"

readonly SRC_DIR_2="${ROOT}/src2"
mkdir -p "${SRC_DIR}/${NESTED_DIR}"
cp -R "${SRC_DIR}" "${SRC_DIR_2}"
echo "updated content" > "${SRC_DIR_2}/${NESTED_DIR}/file"

readonly MAIN_ROOT="${ROOT}/main"
mkdir -p "${MAIN_ROOT}"
cd "${MAIN_ROOT}"

echo "Creating repo-config at ${MAIN_ROOT}:"
cat > repo-config.json << EOF
{ "repositories":
    { "targets": 
        { "repository":
            { "type": "file"
            , "path": "${TARGET_ROOT}"
            , "pragma": {"to_git": true}
            }
        }
    , "initial_sources":
        { "repository":
            { "type": "file"
            , "path": "${SRC_DIR}"
            , "pragma": {"to_git": true}
            }
        , "target_root": "targets"
        , "target_file_name": "TARGETS.install_structure"
        }
    , "updated_sources":
        { "repository":
            { "type": "file"
            , "path": "${SRC_DIR_2}"
            , "pragma": {"to_git": true}
            }
        , "target_root": "targets"
        , "target_file_name": "TARGETS.install_structure"
        }
    , "tree_structure_1":
        { "repository": 
            { "type": "tree structure"
            , "repo": "initial_sources"
            }
        , "target_root": "targets"
        , "target_file_name": "TARGETS.ls_structure"
        }
    , "tree_structure_2":
        { "repository": 
            { "type": "tree structure"
            , "repo": "updated_sources"
            }
        , "target_root": "targets"
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

echo "JustMR setup for tree_structure_1:"
readonly CONF_1=$("${JUST_MR}" --norc --local-build-root "${LBRDIR}" \
                 -C repo-config.json --main tree_structure_1  setup)
cat "${CONF_1}"
echo

echo "Building tree_structure_1 (expected a new cache entry):"
echo
("${JUST_MR}" --norc --local-build-root "${LBRDIR}" -C repo-config.json \
            --main tree_structure_1  --just "${JUST}" \
            install -L '["env", "PATH='"${PATH}"'"]' "${COMPAT}" \
            --log-limit 4 -o "${OUT}/tree_structure_1" 2>&1) > "${OUT}/log"

cat "${OUT}/log"
echo
grep 'Export target \["@","tree_structure_1","",""\] registered for caching' \
      "${OUT}/log"

echo "JustMR setup for tree_structure_2:"
readonly CONF_2=$("${JUST_MR}" --norc --local-build-root "${LBRDIR}" \
                 -C repo-config.json --main tree_structure_2  setup)
cat "${CONF_2}"
echo

echo "Building tree_structure_2 (expected to be taken from cache):"
echo
("${JUST_MR}" --norc --local-build-root "${LBRDIR}" -C repo-config.json \
            --main tree_structure_2  --just "${JUST}" \
            install -L '["env", "PATH='"${PATH}"'"]' "${COMPAT}" \
            --log-limit 4 -o "${OUT}/tree_structure_2" 2>&1) > "${OUT}/log2"

cat "${OUT}/log2"
echo
grep 'Export target \["@","tree_structure_2","",""\] taken from cache' \
      "${OUT}/log2"

echo OK
