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

readonly JUST_LOCK="${PWD}/bin/lock-tool-under-test"
readonly REPO_DIRS="${TEST_TMPDIR}/repos"
readonly WRKDIR="${PWD}"

mkdir -p "${REPO_DIRS}/foo/"
cd "${REPO_DIRS}/foo"
cat > repos.json <<'EOF'
{ "repositories":
  { "":
    {"repository": {"type": "file", "path": "src"}, "bindings": {"bar": "bar"}}
  , "bar":
    { "repository":
      { "type": "archive"
      , "content": "4d86bdf4d07535db3754e6cbf30a6fc81aaa2cc1"
      , "fetch": "https://example.org/v1.2.3"
      }
    }
  }
}
EOF
git init 2>&1
git checkout --orphan foomaster 2>&1
git config user.name 'N.O.Body' 2>&1
git config user.email 'nobody@example.org' 2>&1
git add . 2>&1
git commit -m 'Add foo' 2>&1

mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > repos.in.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings":
      { "foo": "foo"
      , "bar": "my/internal/bar/version"
      , "file-foo": "file-foo"
      , "file-bar": "file-bar"
      }
    }
  , "my/internal/bar/version":
    { "repository":
      { "type": "archive"
      , "content": "4d86bdf4d07535db3754e6cbf30a6fc81aaa2cc1"
      , "fetch": "https://example.org/v1.2.3"
      }
    }
  , "file-foo":
    { "repository":
      {"type": "file", "path": "third_party", "pragma": {"to_git": true}}
    }
  , "file-bar": {"repository": {"type": "file", "path": "third_party"}}
  }
, "imports":
  [ { "source": "git"
    , "repos":
      [ { "alias": "foo"
        , "pragma": {"absent": true}
        }
      ]
    , "url": "${REPO_DIRS}/foo"
    , "mirrors": ["http://primary.example.org/foo"]
    , "branch": "foomaster"
    , "inherit_env": ["AUTH_VAR_FOO"]
    }
  ]
}
EOF
"${JUST_LOCK}" -C repos.in.json -o repos.json
cat repos.json
echo

# Both bar repos should be unified by deduplication
FOO_BAR=$(jq '.repositories.foo.bindings.bar' repos.json)
echo "Bar repo of foo: ${FOO_BAR}"
INTERNAL_BAR=$(jq '.repositories."".bindings.bar' repos.json)
echo "Internal bar repo: ${INTERNAL_BAR}"
[ "${FOO_BAR}" = "${INTERNAL_BAR}" ]
jq '.repositories.'"$FOO_BAR" repos.json > bar.json
cat bar.json
# ... and the pragma empty, as the internal bar is part of the merge
# and that repository cannot be absent.
[ $(jq '.pragma?' bar.json) = "null" ]

# Also, both file repositories should be unified
FILE_FOO=$(jq '.repositories."".bindings."file-foo"' repos.json)
echo "file-foo of '' is now ${FILE_FOO}"
FILE_BAR=$(jq '.repositories."".bindings."file-bar"' repos.json)
echo "file-bar of '' is now ${FILE_BAR}"
[ "${FILE_FOO}" = "${FILE_BAR}" ]
jq '.repositories.'"$FILE_FOO" repos.json > local.json
cat local.json
# The to_git pragma should be preserved
[ $(jq '.repository.pragma."to_git"' local.json) = "true" ]

echo OK
