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

readonly DEDUPLICATE="${PWD}/bin/deduplicate-tool-under-test"
readonly JUST_LOCK="${PWD}/bin/lock-tool-under-test"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/build-output"
readonly REPO_DIRS="${TEST_TMPDIR}/repos"
readonly WRKDIR="${PWD}"

mkdir -p "${REPO_DIRS}/foo/src"
cd "${REPO_DIRS}/foo"
cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": "src"}}}}
EOF
cat > src/TARGETS <<'EOF'
{ "":
  {"type": "generic", "outs": ["foo.txt"], "cmds": ["echo -n FOO > foo.txt"]}
}
EOF
git init
git checkout --orphan foomaster
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git add .
git commit -m 'Add foo.txt' 2>&1


mkdir -p "${REPO_DIRS}/bar"
cd "${REPO_DIRS}/bar"
cat > repos.in.json <<EOF
{ "repositories":
  { "":
    {"repository": {"type": "file", "path": ""}, "bindings": {"foo": "foo"}}
  }
, "imports":
  [ { "source": "git"
    , "repos": [{"alias": "foo"}]
    , "url": "${REPO_DIRS}/foo"
    , "branch": "foomaster"
    }
  ]
}
EOF
echo
"${JUST_LOCK}" -C repos.in.json -o repos.json --local-build-root "${LBR}" 2>&1
echo
cat repos.json
echo
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["bar.txt"]
  , "cmds": ["cat foo.txt | tr A-Z a-z > bar.txt"]
  , "deps": [["@", "foo", "", ""]]
  }
}
EOF
git init
git checkout --orphan barmaster
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git add .
git commit -m 'Add bar.txt' 2>&1

mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "cmds": ["cat foo.txt bar.txt > out.txt"]
  , "outs": ["out.txt"]
  , "deps": [["@", "foo", "", ""], ["@", "bar", "", ""]]
  }
}
EOF

# Check "keep" field works by keeping both version of foo (direct and transient)
cat > repos.in.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo", "bar": "bar"}
    }
  }
, "imports":
  [ { "source": "git"
    , "repos": [{"alias": "foo"}]
    , "url": "${REPO_DIRS}/foo"
    , "branch": "foomaster"
    }
  , { "source": "git"
    , "repos": [{"alias": "bar"}]
    , "url": "${REPO_DIRS}/bar"
    , "branch": "barmaster"
    }
  ]
, "keep": ["foo", "bar/foo"]
}
EOF
echo
"${JUST_LOCK}" -C repos.in.json -o repos-keep.json --local-build-root "${LBR}" 2>&1
echo
cat repos-keep.json
echo
"${JUST_MR}" -C repos-keep.json --norc --just "${JUST}" \
             --local-build-root "${LBR}" analyse \
             --dump-plain-graph actions-keep.json 2>&1
echo

# Check that deduplication (performed by default) works as expected
cat > repos.in.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo", "bar": "bar"}
    }
  }
, "imports":
  [ { "source": "git"
    , "repos": [{"alias": "foo"}]
    , "url": "${REPO_DIRS}/foo"
    , "branch": "foomaster"
    }
  , { "source": "git"
    , "repos": [{"alias": "bar"}]
    , "url": "${REPO_DIRS}/bar"
    , "branch": "barmaster"
    }
  ]
}
EOF
echo
"${JUST_LOCK}" -C repos.in.json -o repos.json --local-build-root "${LBR}" 2>&1
echo
cat repos.json
echo
"${JUST_MR}" -C repos.json --norc --just "${JUST}" \
             --local-build-root "${LBR}" analyse \
             --dump-plain-graph actions.json 2>&1
echo
# Check against existing tooling
cat repos-keep.json | "${DEDUPLICATE}" > repos-dedup.json
jq . repos.json > file_a.json
jq . repos-dedup.json > file_b.json
cmp file_a.json file_b.json

# Verify that deduplication reduces the number of repositories, but does
# not change the action graph (except for the origins of the actions).
[ $(jq -aM '.repositories | length' "repos.json") -lt $(jq -aM '.repositories | length' "repos-keep.json") ]
cmp actions-keep.json actions.json

echo "OK"
