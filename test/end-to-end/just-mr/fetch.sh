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



# Call just-mr with distdir present, to make it aware of the file
"${JUST_MR}" --norc --local-build-root "${LBR}" --distdir "${DISTDIR}" setup 2>&1

# Remove distdir content
rm -rf "${DISTDIR}"
mkdir -p "${DISTDIR}"

# Ask just-mr to fetch to the empty distdir
"${JUST_MR}" --norc --local-build-root "${LBR}" fetch -o "${DISTDIR}" 2>&1

# Verify that the correct file is stored in the distdir
test -f "${DISTDIR}/foo-1.2.3.tar"
newfoocontent=$(git hash-object "${DISTDIR}/foo-1.2.3.tar")
echo "Foo archive has now content ${newfoocontent}"
test "${newfoocontent}" = "${foocontent}"

# Verify that fetching accepts distfiles already present
"${JUST_MR}" --norc --local-build-root "${LBR}" fetch -o "${DISTDIR}" 2>&1
newfoocontent=$(git hash-object "${DISTDIR}/foo-1.2.3.tar")
echo "Foo archive has now content ${newfoocontent}"
test "${newfoocontent}" = "${foocontent}"

echo OK
