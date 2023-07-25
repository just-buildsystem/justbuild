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

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly DISTDIR="${TEST_TMPDIR}/distfiles"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly INSTALL_DIR="${TEST_TMPDIR}/installation-target"

readonly TEST_DATA="The content of the data file in foo"
readonly TEST_PATH="bar/baz"
readonly LINK_TARGET="dummy"

mkdir -p "${DISTDIR}"

# Set up sample archive
mkdir -p "foo/${TEST_PATH}"
echo {} > foo/TARGETS
echo -n "${TEST_DATA}" > "foo/${TEST_PATH}/data.txt"
ln -s "${LINK_TARGET}" "foo/${TEST_PATH}/link"
tar cf "${DISTDIR}/foo-1.2.3.tar" foo 2>&1
foocontent=$(git hash-object "${DISTDIR}/foo-1.2.3.tar")
echo "Foo archive has content ${foocontent}"
rm -rf foo

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
  , "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo"}
    }
  }
}
EOF

# Compute the repository configuration
CONF=$("${JUST_MR}" --norc --local-build-root "${LBR}" --distdir "${DISTDIR}" setup)
cat "${CONF}"
echo

#  Read tree from repository configuration
TREE=$(jq -r '.repositories.foo.workspace_root[1]' "${CONF}")
echo Tree is "${TREE}"

# As the tree is known to just (in the git CAS), we should be able to install
# it with install-cas
"${JUST}" install-cas --local-build-root "${LBR}" -o "${INSTALL_DIR}" \
          "${TREE}::t" 2>&1
test "$(cat "${INSTALL_DIR}/${TEST_PATH}/data.txt")" = "${TEST_DATA}"
test "$(readlink "${INSTALL_DIR}/${TEST_PATH}/link")" = "${LINK_TARGET}"

echo OK
