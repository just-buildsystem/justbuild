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


###
# This test checks if `just add-to-cas` correctly resolves symlinks.
##

set -eu

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly DISTDIR="${TEST_TMPDIR}/distfiles"

readonly LBR_SETUP="${TEST_TMPDIR}/local-build-root-setup"
readonly LBR_A="${TEST_TMPDIR}/local-build-root-a"
readonly LBR_B="${TEST_TMPDIR}/local-build-root-b"
readonly LBR_C="${TEST_TMPDIR}/local-build-root-c"
readonly LBR_D="${TEST_TMPDIR}/local-build-root-d"
readonly LBR_E="${TEST_TMPDIR}/local-build-root-e"
readonly LBR_F="${TEST_TMPDIR}/local-build-root-f"
readonly LBR_G="${TEST_TMPDIR}/local-build-root-g"
readonly LBR_H="${TEST_TMPDIR}/local-build-root-h"
readonly LBR_I="${TEST_TMPDIR}/local-build-root-i"
readonly LBR_J="${TEST_TMPDIR}/local-build-root-j"
readonly LBR_K="${TEST_TMPDIR}/local-build-root-k"

readonly UNPACK_DIR="${TEST_TMPDIR}/unpack-dir"

readonly TEST_DATA="The content of the data file in foo"
readonly TEST_DIRS="bar/baz"
readonly DATA_PATH="bar/baz/data.txt"
readonly NON_UPWARDS_LINK_PATH="bar/baz/nonupwards"
readonly NON_UPWARDS_LINK_TARGET="data.txt"
readonly NON_UPWARDS_REWRITTEN_LINK_TARGET="../../../data.txt"
readonly UPWARDS_LINK_PATH="bar/upwards"
readonly UPWARDS_LINK_TARGET="../bar/baz"
readonly INDIRECT_LINK_PATH="bar/indirect"
readonly INDIRECT_LINK_TARGET="upwards/data.txt"
readonly CYCLE_LINK_1_PATH="bar/cycle_1"
readonly CYCLE_LINK_1_TARGET="cycle_2"
readonly CYCLE_LINK_2_PATH="bar/cycle_2"
readonly CYCLE_LINK_2_TARGET="cycle_1"

mkdir -p "${DISTDIR}"

mkdir -p log
LOGDIR="$(realpath log)"
LOG_ARCHIVE_REPO="${LOGDIR}/archive.txt"
LOG_FILE_REPO="${LOGDIR}/file.txt"

###
# Set up the archives
##

ROOT=$(pwd)
mkdir -p "foo/${TEST_DIRS}"
echo {} > foo/TARGETS
echo -n "${TEST_DATA}" > "foo/${DATA_PATH}"

# add resolvable non-upwards symlink
ln -s "${NON_UPWARDS_LINK_TARGET}" "foo/${NON_UPWARDS_LINK_PATH}"
tar cf "${DISTDIR}/foo-1.2.3_non_upwards.tar" foo \
  --sort=name --owner=root:0 --group=root:0 --mtime='UTC 1970-01-01' 2>&1
CONTENT_NON_UPWARDS=$(git hash-object "${DISTDIR}/foo-1.2.3_non_upwards.tar")
echo "Foo archive with resolvable non-upwards symlinks has content ${CONTENT_NON_UPWARDS}"

# now add also a resolvable upwards symlink
ln -s "${UPWARDS_LINK_TARGET}" "foo/${UPWARDS_LINK_PATH}"
# ...and a resolvable non-upwards symlink pointing to it
ln -s "${INDIRECT_LINK_TARGET}" "foo/${INDIRECT_LINK_PATH}"
tar cf "${DISTDIR}/foo-1.2.3_upwards.tar" foo \
  --sort=name --owner=root:0 --group=root:0 --mtime='UTC 1970-01-01' 2>&1
CONTENT_UPWARDS=$(git hash-object "${DISTDIR}/foo-1.2.3_upwards.tar")
echo "Foo archive with resolvable upwards symlinks has content ${CONTENT_UPWARDS}"

# now add also cycles, to check for failures to resolve
ln -s "${CYCLE_LINK_1_TARGET}" "foo/${CYCLE_LINK_1_PATH}"
ln -s "${CYCLE_LINK_2_TARGET}" "foo/${CYCLE_LINK_2_PATH}"
tar cf "${DISTDIR}/foo-1.2.3_cycle.tar" foo \
  --sort=name --owner=root:0 --group=root:0 --mtime='UTC 1970-01-01' 2>&1
CONTENT_CYCLES=$(git hash-object "${DISTDIR}/foo-1.2.3_cycle.tar")
echo "Foo archive with symlink cycles has content ${CONTENT_CYCLES}"

###
# Setup sample repository config
##

touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "foo_non_upwards_unresolved":
    { "repository":
      { "type": "archive"
      , "content": "${CONTENT_NON_UPWARDS}"
      , "fetch": "http://non-existent.example.org/foo-1.2.3_non_upwards.tar"
      , "subdir": "foo"
      }
    }
  , "foo_non_upwards_resolved":
    { "repository":
      { "type": "archive"
      , "content": "${CONTENT_NON_UPWARDS}"
      , "fetch": "http://non-existent.example.org/foo-1.2.3_non_upwards.tar"
      , "subdir": "foo"
      , "pragma": {"special": "resolve-completely"}
      }
    }
  , "foo_upwards_ignore_special":
    { "repository":
      { "type": "archive"
      , "content": "${CONTENT_UPWARDS}"
      , "fetch": "http://non-existent.example.org/foo-1.2.3_upwards.tar"
      , "subdir": "foo"
      , "pragma": {"special": "ignore"}
      }
    }
  , "foo_upwards_resolve_partially":
    { "repository":
      { "type": "archive"
      , "content": "${CONTENT_UPWARDS}"
      , "fetch": "http://non-existent.example.org/foo-1.2.3_upwards.tar"
      , "subdir": "foo"
      , "pragma": {"special": "resolve-partially"}
      }
    }
  , "foo_upwards_resolve_completely":
    { "repository":
      { "type": "archive"
      , "content": "${CONTENT_UPWARDS}"
      , "fetch": "http://non-existent.example.org/foo-1.2.3_upwards.tar"
      , "subdir": "foo"
      , "pragma": {"special": "resolve-completely"}
      }
    }
  }
}
EOF

# Compute the repository configuration
CONF=$("${JUST_MR}" --norc --local-build-root "${LBR_SETUP}" \
                    --distdir "${DISTDIR}" setup --all)
cat "${CONF}"
echo

###
# Compare add-to-cas trees to set up roots
##

# Check archive with only non-upwards symlinks

mkdir -p "${UNPACK_DIR}"
tar xf "${DISTDIR}/foo-1.2.3_non_upwards.tar" -C "${UNPACK_DIR}"

echo === check non-upwards symlinks default ===

COMPUTED=$("${JUST}" add-to-cas --local-build-root "${LBR_A}" "${UNPACK_DIR}/foo") 2>&1
EXPECTED=$(jq -r '.repositories.foo_non_upwards_unresolved.workspace_root[1]' "${CONF}")
[ "${COMPUTED}" = "${EXPECTED}" ]
echo done

echo === check non-upwards symlinks resolve tree-upwards same as default ===

COMPUTED=$("${JUST}" add-to-cas --local-build-root "${LBR_B}" \
                                --resolve-special=tree-upwards "${UNPACK_DIR}/foo") 2>&1
EXPECTED=$(jq -r '.repositories.foo_non_upwards_unresolved.workspace_root[1]' "${CONF}")
[ "${COMPUTED}" = "${EXPECTED}" ]
echo done

echo === check non-upwards symlinks resolve tree-all ===

COMPUTED=$("${JUST}" add-to-cas --local-build-root "${LBR_C}" \
                                --resolve-special=tree-all "${UNPACK_DIR}/foo") 2>&1
EXPECTED=$(jq -r '.repositories.foo_non_upwards_resolved.workspace_root[1]' "${CONF}")
[ "${COMPUTED}" = "${EXPECTED}" ]
echo done

echo === check non-upwards symlinks resolve all same as tree-all ===

COMPUTED=$("${JUST}" add-to-cas --local-build-root "${LBR_D}" \
                                --resolve-special=tree-all "${UNPACK_DIR}/foo") 2>&1
EXPECTED=$(jq -r '.repositories.foo_non_upwards_resolved.workspace_root[1]' "${CONF}")
[ "${COMPUTED}" = "${EXPECTED}" ]
echo done

# Check archive with upwards symlinks confined to the tree

rm -rf "${UNPACK_DIR}/*"
tar xf "${DISTDIR}/foo-1.2.3_upwards.tar" -C "${UNPACK_DIR}"

echo === check confined upwards symlinks resolve ignore ===

COMPUTED=$("${JUST}" add-to-cas --local-build-root "${LBR_E}" \
                                --resolve-special=ignore "${UNPACK_DIR}/foo") 2>&1
EXPECTED=$(jq -r '.repositories.foo_upwards_ignore_special.workspace_root[1]' "${CONF}")
[ "${COMPUTED}" = "${EXPECTED}" ]
echo done

echo === check confined upwards symlinks resolve tree-upwards ===

COMPUTED=$("${JUST}" add-to-cas --local-build-root "${LBR_F}" \
                                --resolve-special=tree-upwards "${UNPACK_DIR}/foo") 2>&1
EXPECTED=$(jq -r '.repositories.foo_upwards_resolve_partially.workspace_root[1]' "${CONF}")
[ "${COMPUTED}" = "${EXPECTED}" ]
echo done

echo === check confined upwards symlinks resolve tree-all ===

COMPUTED=$("${JUST}" add-to-cas --local-build-root "${LBR_G}" \
                                --resolve-special=tree-all "${UNPACK_DIR}/foo") 2>&1
EXPECTED=$(jq -r '.repositories.foo_upwards_resolve_completely.workspace_root[1]' "${CONF}")
[ "${COMPUTED}" = "${EXPECTED}" ]
echo done

echo === check confined upwards symlinks resolve all same as tree-all ===

COMPUTED=$("${JUST}" add-to-cas --local-build-root "${LBR_H}" \
                                --resolve-special=all "${UNPACK_DIR}/foo") 2>&1
EXPECTED=$(jq -r '.repositories.foo_upwards_resolve_completely.workspace_root[1]' "${CONF}")
[ "${COMPUTED}" = "${EXPECTED}" ]
echo done

echo === check confined upwards symlinks default fail ===

"${JUST}" add-to-cas --local-build-root "${LBR_I}" "${UNPACK_DIR}/foo" 2>&1 && exit 1 || :
echo failed as expected

# Check non-confined upwards symlinks

# replace non-upwards link to make it upwards pointing to file copy outside
# tree; resulting resolved tree should be the same
cp "${UNPACK_DIR}/foo/${DATA_PATH}" "${UNPACK_DIR}"
rm "${UNPACK_DIR}/foo/${NON_UPWARDS_LINK_PATH}"
ln -s "${NON_UPWARDS_REWRITTEN_LINK_TARGET}" "${UNPACK_DIR}/foo/${NON_UPWARDS_LINK_PATH}"

echo === check non-confined upwards symlinks resolve all ===

COMPUTED=$("${JUST}" add-to-cas --local-build-root "${LBR_J}" \
                                --resolve-special=all "${UNPACK_DIR}/foo") 2>&1
EXPECTED=$(jq -r '.repositories.foo_upwards_resolve_completely.workspace_root[1]' "${CONF}")
[ "${COMPUTED}" = "${EXPECTED}" ]
echo done

# Check that cycles are detected

rm -rf "${UNPACK_DIR}/*"
tar xf "${DISTDIR}/foo-1.2.3_cycle.tar" -C "${UNPACK_DIR}"

echo === check cycle fail ===

"${JUST}" add-to-cas --local-build-root "${LBR_K}" \
                     --resolve-special=all "${UNPACK_DIR}/foo" 2>&1 && exit 1 || :
echo failed as expected

echo OK
