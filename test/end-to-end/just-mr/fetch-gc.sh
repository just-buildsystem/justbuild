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
readonly FETCH_TO_DIR="${TEST_TMPDIR}/directory-to-fetch-to"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly INSTALL_DIR="${TEST_TMPDIR}/installation-target"

readonly TEST_DATA="The content of the data file in foo"

mkdir -p "${DISTDIR}"
mkdir -p "${FETCH_TO_DIR}"

mkdir -p foo/bar/baz
echo {} > foo/TARGETS
echo -n "${TEST_DATA}" > foo/bar/baz/data.txt
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
echo "Repository configuration:"
cat repos.json
echo
cat > TARGETS <<'EOF'
{ "":
  { "type": "install"
  , "files": {"out.txt": ["@", "foo", "", "bar/baz/data.txt"]}
  }
}
EOF

# Call just-mr setup. This will also create a cache entry for the tree
# corresponding to that archive
"${JUST_MR}" --local-build-root "${LBR}" --distdir "${DISTDIR}" setup 2>&1

# Remove entry from CAS
"${JUST}" gc --local-build-root "${LBR}" 2>&1
"${JUST}" gc --local-build-root "${LBR}" 2>&1

# Fetch should still work, if given access to the original file
"${JUST_MR}" --local-build-root "${LBR}" --distdir "${DISTDIR}" \
             fetch -o "${FETCH_TO_DIR}" 2>&1
newfoocontent=$(git hash-object "${DISTDIR}/foo-1.2.3.tar")
echo "Foo archive has now content ${newfoocontent}"
test "${newfoocontent}" = "${foocontent}"

# Remove entry again from CAS and clear distdirs
"${JUST}" gc --local-build-root "${LBR}" 2>&1
"${JUST}" gc --local-build-root "${LBR}" 2>&1
rm -rf "${DISTDIR}"
rm -rf "${FETCH_TO_DIR}"

# Setup for building should still be possible
"${JUST_MR}" --just "${JUST}" --local-build-root "${LBR}" \
             install -o "${INSTALL_DIR}" 2>&1
test "$(cat "${INSTALL_DIR}/out.txt")" = "${TEST_DATA}"

echo OK
