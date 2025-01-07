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

readonly SRC_DIR="${ROOT}/src"
mkdir -p "${SRC_DIR}/test_dir/nested_dir/nested_dir_2"
echo "initial content" > "${SRC_DIR}/test_dir/nested_dir/nested_dir_2/file"

readonly MAIN_ROOT="${ROOT}/main"
mkdir -p "${MAIN_ROOT}"
cd "${MAIN_ROOT}"

cat > TARGETS << 'EOF'
{ "" : {"type": "export", "target": "ls"}
, "ls":
  { "type": "generic"
  , "deps": [["TREE", null, "test_dir"]]
  , "outs": ["result.txt"]
  , "cmds": ["ls -R test_dir > result.txt"]
  }
}
EOF

echo "Creating repo-config at ${MAIN_ROOT}:"
cat > repo-config.json << EOF
{ "repositories":
  { "src":
    { "repository":
      { "type": "file"
      , "path": "${SRC_DIR}"
      , "pragma": {"to_git": true}
      }
    }
  , "src_indirection": { "repository": "src" }
  , "src_structure":
    { "repository": 
      { "type": "tree structure"
      , "repo": "src_indirection"
      }
    }
  , "src_structure_indirection": { "repository": "src_structure" }
  , "src_structure_2":
    { "repository":
      { "type": "tree structure"
      , "repo": "src_structure_indirection"
      }
    }
  , "targets":
    { "repository":
      { "type": "file"
      , "path": "."
      , "pragma": {"to_git": true}
      }
    }
  , "src_structure_indirection_2":
    { "repository": "src_structure_2" }
  , "src_structure_indirection_3":
    { "repository": "src_structure_indirection_2" }
  , "result":
    { "repository": "src_structure_indirection_3"
    , "target_root": "targets"
    }
  }
}
EOF
cat repo-config.json
echo

echo "JustMR setup:"
readonly CONF=$("${JUST_MR}" --norc --local-build-root "${LBRDIR}" \
                 -C repo-config.json --main result  setup)
cat "${CONF}"
echo

echo "Build:"
echo
"${JUST}" install "${COMPAT}" -L '["env", "PATH='"${PATH}"'"]' \
    --local-build-root "${LBRDIR}" -C "${CONF}" -o "${OUT}/result" 2>&1

echo
cat "${OUT}/result/result.txt"

echo OK
