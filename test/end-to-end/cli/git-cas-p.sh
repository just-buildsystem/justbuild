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
set -e

ROOT="$(pwd)"
JUST="${ROOT}/bin/tool-under-test"
JUST_MR="${ROOT}/bin/mr-tool-under-test"
BUILDROOT="${TEST_TMPDIR}/build-root"
DISTDIR="${TEST_TMPDIR}/distfiles"
mkdir -p "${DISTDIR}"


# Create an archive
mkdir -p src/with/some/deep/directory
echo 'Hello World' > src/with/some/deep/directory/hello.txt
tar cvf "${DISTDIR}/src.tar" src
rm -rf src
HASH=$(git hash-object "${DISTDIR}/src.tar")

echo
echo archive has content $HASH
echo

# Set up a build root from that archive
mkdir work
cd work
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "archive"
      , "content": "${HASH}"
      , "fetch": "http://example.com/src.tar"
      , "subdir": "src/with"
      }
    }
  }
}
EOF
cat repos.json
echo
"${JUST_MR}" --norc --local-build-root "${BUILDROOT}" \
             --distdir "${DISTDIR}" setup > CONF
echo
cat CONF
echo
cat $(cat CONF)
echo
TREE=$(jq -rM '.repositories."".workspace_root[1]' $(cat CONF))
echo
echo Tree is ${TREE}
echo

# Verify that we can obtain this tree, as well as its parts
"${JUST}" install-cas --local-build-root "${BUILDROOT}" "${TREE}::t"
"${JUST}" install-cas --local-build-root "${BUILDROOT}" "${TREE}::t" -P some
"${JUST}" install-cas --local-build-root "${BUILDROOT}" "${TREE}::t" -P some/deep
"${JUST}" install-cas --local-build-root "${BUILDROOT}" "${TREE}::t" -P some/deep/directory
"${JUST}" install-cas --local-build-root "${BUILDROOT}" "${TREE}::t" -P some/deep/directory/hello.txt
"${JUST}" install-cas --local-build-root "${BUILDROOT}" "${TREE}::t" -P some/deep/directory/hello.txt -o hello.txt
grep World hello.txt

echo
echo OK
