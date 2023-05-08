#!/bin/sh
# Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly DISTDIR="${TEST_TMPDIR}/distfiles"
readonly LBR="${TEST_TMPDIR}/local-build-root"

mkdir -p "${DISTDIR}"
mkdir -p foo/bar/baz
echo "test data" > foo/bar/baz/data.txt
tar cf "${DISTDIR}/foo-1.2.3.tar" foo 2>&1
foocontent=$(git hash-object "${DISTDIR}/foo-1.2.3.tar")
echo "Foo archive has content ${foocontent}"

# get the tree identifier for later sanity check
cd foo
git init
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git add .
git commit -m 'Just care about the tree' 2>&1
tree_id=$(git log -n 1 --format='%T')
cd ..
rm -rf foo
echo "foo as tree ${tree_id}"

# Setup sample repository config
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "foo":
    { "repository":
      { "type": "archive"
      , "content": "${foocontent}"
      , "fetch": "http://non-existent.example.org/foo-1.2.3.tar"
      , "subdir": "foo"
      }
    }
  , "distdir": {"repository": {"type": "distdir", "repositories": ["foo"]}}
  , "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo", "distdir": "distdir"}
    }
  }
}
EOF
echo "Repository configuration:"
cat repos.json


# Call just-mr with distdir present
FIRST_CONFIG=$("${JUST_MR}" --norc --local-build-root "${LBR}" --distdir "${DISTDIR}" setup)
echo "Config on first set (with everything available) ${FIRST_CONFIG}"
cat "${FIRST_CONFIG}"

# sanity-check the config: foo has to have the correct git root
[ $(jq '."repositories"."foo"."workspace_root" | .[1]' "${FIRST_CONFIG}") = "\"${tree_id}\"" ]

echo "First setup done successfully"

# Remove CAS and distfiles
rm -rf "${LBR}/protocol-dependent"
ls -al "${LBR}"
rm -rf "${DISTDIR}"

# Call just-mr again, with map-files and git prefilled from first run,
# but CAS and distdir missing.
SECOND_CONFIG=$("${JUST_MR}" --norc --local-build-root "${LBR}" setup)
[ -f "${SECOND_CONFIG}" ]
[ "${FIRST_CONFIG}" = "${SECOND_CONFIG}" ]

echo OK
