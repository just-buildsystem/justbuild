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
readonly DEDUPLICATE="${ROOT}/bin/deduplicate-tool-under-test"
readonly GIT_IMPORT="${ROOT}/bin/git-import-under-test"
readonly JUST="${ROOT}/bin/tool-under-test"
readonly JUST_MR="${ROOT}/bin/mr-tool-under-test"

readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/build-output"
readonly REPO_DIRS="${TEST_TMPDIR}/repos"

mkdir -p "${OUT}"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

# Set up repo foo
readonly FOO_REPO="${REPO_DIRS}/foo"
readonly FOO_NESTED_DIR="${FOO_REPO}/src/nested_dir/nested_dir_2"
mkdir -p "${FOO_NESTED_DIR}"
echo "content" > "${FOO_NESTED_DIR}/file"
cd "${FOO_REPO}"

echo
echo "Creating TARGETS at ${FOO_REPO}:"
cat > TARGETS << 'EOF'
{ "": {"type": "export", "target": "test"}
, "test":
  { "type": "install"
  , "deps": [["TREE", null, "src"]]
  }
}
EOF

echo
echo "Creating repos.json at ${FOO_REPO}:"
cat > repos.json <<'EOF'
{ "repositories":
  { "":
    { "repository":
      { "type": "file"
      , "path": "."
      , "pragma": {"to_git": true}
      }
    }
  }
}
EOF
cat repos.json

echo
git init
git checkout --orphan foomaster
git config user.name 'Nobody'
git config user.email 'nobody@example.org'
git add .
git commit -m 'Add foo' 2>&1

# Set up repo bar
readonly BAR_REPO="${REPO_DIRS}/bar"
mkdir -p "${BAR_REPO}"
cd "${BAR_REPO}"

echo
echo "Creating repos.template.json at ${BAR_REPO}:"
cat > repos.template.json << EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "tree structure"
      , "repo": "foo"
      }
    }
  }
}
EOF
cat repos.template.json

# Import foo to bar
"${GIT_IMPORT}" -C repos.template.json --as foo -b foomaster "${REPO_DIRS}/foo" \
      2>&1 > repos.json

echo repos.json
cat repos.json

echo
git init
git checkout --orphan barmaster
git config user.name 'Nobody'
git config user.email 'nobody@example.org'
git add .
git commit -m 'Add bar' 2>&1

# Set up main repo
readonly MAIN_ROOT="${REPO_DIRS}/main"
mkdir -p "${MAIN_ROOT}"
cd "${MAIN_ROOT}"
touch ROOT

echo
echo "Creating TARGETS at ${MAIN_ROOT}:"
cat > TARGETS << 'EOF'
{ "": {"type": "export", "target": "gen"}
, "gen":
  { "type": "generic"
  , "cmds": ["ls -R src > out.txt"]
  , "outs": ["out.txt"]
  , "deps": [["TREE", null, "src"]]
  }
}
EOF
cat TARGETS

echo
echo "Creating TARGETS.result at ${MAIN_ROOT}:"
cat > TARGETS.result << 'EOF'
{ "rename_1":
  { "type": "generic"
  , "cmds": ["mv out.txt foo.txt"]
  , "outs": ["foo.txt"]
  , "deps": [["@", "structure_1", "", ""]]
  }
, "rename_2":
  { "type": "generic"
  , "cmds": ["mv out.txt bar.txt"]
  , "outs": ["bar.txt"]
  , "deps": [["@", "structure_2", "", ""]]
  }
, "":
  { "type": "generic"
  , "cmds": ["cat foo.txt bar.txt > out.txt"]
  , "outs": ["out.txt"]
  , "deps": ["rename_1", "rename_2"]
  }
}
EOF
cat TARGETS.result

echo
echo "Creating repos.template.json at ${MAIN_ROOT}:"
cat > repos.template.json << EOF
{ "repositories":
  { "targets":
    { "repository":
      { "type": "file"
      , "path": "."
      , "pragma": {"to_git": true}
      }
    }
  , "structure":
    { "repository": "bar"
    , "target_root": "targets"
    }
  , "structure_2":
    { "repository":
      { "type": "tree structure"
      , "repo": "foo"
      }
    , "target_root": "targets"
    }
  , "result":
    { "repository":
      { "type": "file"
      , "path": "."
      , "pragma": {"to_git": true}
      }
    , "target_root": "targets"
    , "target_file_name": "TARGETS.result"
    , "bindings":
      { "structure_1": "structure"
      , "structure_2": "structure_2"
      }
    }
  }
}
EOF
cat repos.template.json

"${GIT_IMPORT}" -C repos.template.json --as foo -b foomaster "${REPO_DIRS}/foo" \
  | "${GIT_IMPORT}" -C - --as bar -b barmaster "${REPO_DIRS}/bar" \
  2>&1 > repos-full.json

echo repos-full.json
cat repos-full.json

# Dump the graph before deduplication:
echo
"${JUST_MR}" -C repos-full.json --norc --just "${JUST}" \
             --local-build-root "${LBR}" --main "result" \
             -L '["env", "PATH='"${PATH}"'"]' analyse \
             --dump-plain-graph actions-full.json 2>&1

# Run deduplication:
echo
cat repos-full.json | "${DEDUPLICATE}" 2>&1 > repos.json
cat repos.json
echo

# Dump the graph after deduplication:
"${JUST_MR}" -C repos.json --norc --just "${JUST}" \
             --local-build-root "${LBR}" --main "result" \
             -L '["env", "PATH='"${PATH}"'"]' analyse \
             --dump-plain-graph actions.json 2>&1

# Verify that we reduced the number of repositories, but did
# not change the action graph (except for the origins of the actions).
[ $(jq -aM '.repositories | length' repos.json) -lt $(jq -aM '.repositories | length' repos-full.json) ]
cmp actions-full.json actions.json

# Check the result can be built after deduplication:
echo
"${JUST_MR}" -C repos.json --norc --just "${JUST}" \
             --local-build-root "${LBR}" --main "result" \
             -L '["env", "PATH='"${PATH}"'"]' install \
             -o "${OUT}/result" 2>&1

echo OK
