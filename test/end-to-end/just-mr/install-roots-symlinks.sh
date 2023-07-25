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
# This test extends install-roots-basic with 'special' pragma use-cases
##

set -eu

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly DISTDIR="${TEST_TMPDIR}/distfiles"
readonly LBR="${TEST_TMPDIR}/local-build-root"

readonly INSTALL_DIR_RESOLVED="${TEST_TMPDIR}/installation-target-resolved"
readonly INSTALL_DIR_UNRESOLVED="${TEST_TMPDIR}/installation-target-unresolved"
readonly INSTALL_DIR_SPECIAL_IGNORE="${TEST_TMPDIR}/installation-target-special-ignore"
readonly INSTALL_DIR_SPECIAL_PARTIAL="${TEST_TMPDIR}/installation-target-special-partial"
readonly INSTALL_DIR_SPECIAL_COMPLETE="${TEST_TMPDIR}/installation-target-special-complete"

readonly TEST_DATA="The content of the data file in foo"
readonly TEST_DIRS="bar/baz"
readonly DATA_PATH="bar/baz/data.txt"
readonly NON_UPWARDS_LINK_PATH="bar/baz/nonupwards"
readonly NON_UPWARDS_LINK_TARGET="data.txt"
readonly UPWARDS_LINK_PATH="bar/upwards"
readonly UPWARDS_LINK_TARGET="../bar/baz"
readonly INDIRECT_LINK_PATH="bar/indirect"
readonly INDIRECT_LINK_TARGET="upwards/data.txt"

mkdir -p "${DISTDIR}"

###
# Set up sample archives
##

mkdir -p "foo/${TEST_DIRS}"
echo {} > foo/TARGETS
echo -n "${TEST_DATA}" > "foo/${DATA_PATH}"

# add resolvable non-upwards symlink
ln -s "${NON_UPWARDS_LINK_TARGET}" "foo/${NON_UPWARDS_LINK_PATH}"
tar cf "${DISTDIR}/foo-1.2.3_v1.tar" foo \
  --sort=name --owner=root:0 --group=root:0 --mtime='UTC 1970-01-01' 2>&1
foocontent_v1=$(git hash-object "${DISTDIR}/foo-1.2.3_v1.tar")
echo "Foo archive v1 has content ${foocontent_v1}"

# get Git-tree of foo subdir with only non-upwards symlinks
cd foo
(
  git init \
  && git config user.email "nobody@example.org" \
  && git config user.name "Nobody" \
  && git add . \
  && git commit -m "test" --date="1970-01-01T00:00Z"
)
UNRESOLVED_TREE=$(git log -n 1 --format='%T')
echo "Foo archive v1 has unresolved tree ${UNRESOLVED_TREE}"
rm -rf .git
cd ..

# now add also a resolvable upwards symlink
ln -s "${UPWARDS_LINK_TARGET}" "foo/${UPWARDS_LINK_PATH}"
# ...and a resolvable non-upwards symlink pointing to it
ln -s "${INDIRECT_LINK_TARGET}" "foo/${INDIRECT_LINK_PATH}"
tar cf "${DISTDIR}/foo-1.2.3_v2.tar" foo \
  --sort=name --owner=root:0 --group=root:0 --mtime='UTC 1970-01-01' 2>&1
foocontent_v2=$(git hash-object "${DISTDIR}/foo-1.2.3_v2.tar")
echo "Foo archive v2 has content ${foocontent_v2}"
rm -rf foo

###
# Setup sample repository config
##

touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "foo_v1_resolved":
    { "repository":
      { "type": "archive"
      , "content": "${foocontent_v1}"
      , "fetch": "http://non-existent.example.org/foo-1.2.3_v1.tar"
      , "subdir": "foo"
      , "pragma": {"special": "resolve-completely"}
      }
    }
  , "foo_v2_ignore_special":
    { "repository":
      { "type": "archive"
      , "content": "${foocontent_v2}"
      , "fetch": "http://non-existent.example.org/foo-1.2.3_v2.tar"
      , "subdir": "foo"
      , "pragma": {"special": "ignore"}
      }
    }
  , "foo_v2_resolve_partially":
    { "repository":
      { "type": "archive"
      , "content": "${foocontent_v2}"
      , "fetch": "http://non-existent.example.org/foo-1.2.3_v2.tar"
      , "subdir": "foo"
      , "pragma": {"special": "resolve-partially"}
      }
    }
  , "foo_v2_resolve_completely":
    { "repository":
      { "type": "archive"
      , "content": "${foocontent_v2}"
      , "fetch": "http://non-existent.example.org/foo-1.2.3_v2.tar"
      , "subdir": "foo"
      , "pragma": {"special": "resolve-completely"}
      }
    }
  , "":
    { "repository": {"type": "file", "path": "."}
    , "bindings":
      { "foo_v1_resolved": "foo_v1_resolved"
      , "foo_v2_ignore_special": "foo_v2_ignore_special"
      , "foo_v2_resolve_partially": "foo_v2_resolve_partially"
      , "foo_v2_resolve_completely": "foo_v2_resolve_completely"
      }
    }
  }
}
EOF

# Compute the repository configuration
CONF=$("${JUST_MR}" --norc --local-build-root "${LBR}" --distdir "${DISTDIR}" setup)
cat "${CONF}"
echo


###
# Test archives with non-upwards symlinks
##

echo === root with non-upwards symlinks ===

#  Read tree from repository configuration
TREE=$(jq -r '.repositories.foo_v1_resolved.workspace_root[1]' "${CONF}")
echo Tree is "${TREE}"

# As the tree is known to just (in the git CAS), we should be able to install
# it with install-cas
"${JUST}" install-cas --local-build-root "${LBR}" -o "${INSTALL_DIR_RESOLVED}" \
          "${TREE}::t" 2>&1
test "$(cat "${INSTALL_DIR_RESOLVED}/${DATA_PATH}")" = "${TEST_DATA}"
# non-upwards symlink is resolved
test "$(cat "${INSTALL_DIR_RESOLVED}/${NON_UPWARDS_LINK_PATH}")" = "${TEST_DATA}"

# A resolved tree should add to CAS also its unresolved version; in our case,
# the unresolved tree has only non-upwards symlinks, so it can be installed
"${JUST}" install-cas --local-build-root "${LBR}" -o "${INSTALL_DIR_UNRESOLVED}" \
          "${UNRESOLVED_TREE}::t" 2>&1
test "$(cat "${INSTALL_DIR_UNRESOLVED}/${DATA_PATH}")" = "${TEST_DATA}"
test "$(readlink "${INSTALL_DIR_UNRESOLVED}/${NON_UPWARDS_LINK_PATH}")" = "${NON_UPWARDS_LINK_TARGET}"


###
# Test archives with confined symlinks (upwards and non-upwards)
##

echo === root with ignored special entries ===

#  Read tree from repository configuration
TREE=$(jq -r '.repositories.foo_v2_ignore_special.workspace_root[1]' "${CONF}")
echo Tree is "${TREE}"

# As the tree is known to just (in the git CAS), we should be able to install
# it with install-cas
"${JUST}" install-cas --local-build-root "${LBR}" -o "${INSTALL_DIR_SPECIAL_IGNORE}" \
          "${TREE}::t" 2>&1
test "$(cat "${INSTALL_DIR_SPECIAL_IGNORE}/${DATA_PATH}")" = "${TEST_DATA}"
[ ! -e "${INSTALL_DIR_SPECIAL_IGNORE}/${NON_UPWARDS_LINK_PATH}" ]  # symlink should be missing
[ ! -e "${INSTALL_DIR_SPECIAL_IGNORE}/${UPWARDS_LINK_PATH}" ]  # symlink should be missing
[ ! -e "${INSTALL_DIR_SPECIAL_IGNORE}/${INDIRECT_LINK_PATH}" ]  # symlink should be missing


echo === root with partially resolved symlinks ===

#  Read tree from repository configuration
TREE=$(jq -r '.repositories.foo_v2_resolve_partially.workspace_root[1]' "${CONF}")
echo Tree is "${TREE}"

# As the tree is known to just (in the git CAS), we should be able to install
# it with install-cas
"${JUST}" install-cas --local-build-root "${LBR}" -o "${INSTALL_DIR_SPECIAL_PARTIAL}" \
          "${TREE}::t" 2>&1
test "$(cat "${INSTALL_DIR_SPECIAL_PARTIAL}/${DATA_PATH}")" = "${TEST_DATA}"
# non-upwards link remains as-is
test "$(readlink "${INSTALL_DIR_SPECIAL_PARTIAL}/${NON_UPWARDS_LINK_PATH}")" = "${NON_UPWARDS_LINK_TARGET}"
# upwards link is resolved, in this case to a tree
[ -d "${INSTALL_DIR_SPECIAL_PARTIAL}/${UPWARDS_LINK_PATH}" ]
test "$(cat "${INSTALL_DIR_SPECIAL_PARTIAL}/${UPWARDS_LINK_PATH}/data.txt")" = "${TEST_DATA}"
# indirect link is non-upwards, so it should remain as-is
test "$(readlink "${INSTALL_DIR_SPECIAL_PARTIAL}/${INDIRECT_LINK_PATH}")" = "${INDIRECT_LINK_TARGET}"


echo === root with fully resolved symlinks ===

#  Read tree from repository configuration
TREE=$(jq -r '.repositories.foo_v2_resolve_completely.workspace_root[1]' "${CONF}")
echo Tree is "${TREE}"

# As the tree is known to just (in the git CAS), we should be able to install
# it with install-cas
"${JUST}" install-cas --local-build-root "${LBR}" -o "${INSTALL_DIR_SPECIAL_COMPLETE}" \
          "${TREE}::t" 2>&1
test "$(cat "${INSTALL_DIR_SPECIAL_COMPLETE}/${DATA_PATH}")" = "${TEST_DATA}"
# all links get resolved
test "$(cat "${INSTALL_DIR_SPECIAL_COMPLETE}/${NON_UPWARDS_LINK_PATH}")" = "${TEST_DATA}"
[ -d "${INSTALL_DIR_SPECIAL_COMPLETE}/${UPWARDS_LINK_PATH}" ]
test "$(cat "${INSTALL_DIR_SPECIAL_COMPLETE}/${UPWARDS_LINK_PATH}/data.txt")" = "${TEST_DATA}"
test "$(cat "${INSTALL_DIR_SPECIAL_COMPLETE}/${INDIRECT_LINK_PATH}")" = "${TEST_DATA}"

echo OK
