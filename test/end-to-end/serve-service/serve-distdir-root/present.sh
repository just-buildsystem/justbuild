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


###
# This test checks 3 of the options to make a present root for a distidr
# repository, where:
# - archives are in a local distfile;
# - there is already a file association to the distdir root tree;
# - we receive the distdir root from serve endpoint via the remote CAS.
##

set -eu

env

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly DISTDIR="${TEST_TMPDIR}/distfiles"
readonly LBR="${TEST_TMPDIR}/local-build-root"

mkdir -p "${DISTDIR}"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

ENDPOINT_ARGS="-r ${REMOTE_EXECUTION_ADDRESS} -R ${SERVE} ${COMPAT}"

###
# Setup sample repos config with present and absent repos
##

cp src.tar "${DISTDIR}"
SRC_HASH=$(git hash-object src.tar)
cp src-syms.tar "${DISTDIR}"
SRC_SYMS_HASH=$(git hash-object src-syms.tar)

mkdir work
cd work

touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "unresolved":
    { "repository":
      { "type": "archive"
      , "content": "$SRC_HASH"
      , "fetch": "http://example.org/src.tar"
      , "subdir": "repo"
      }
    }
  , "resolved":
    { "repository":
      { "type": "archive"
      , "content": "$SRC_SYMS_HASH"
      , "fetch": "http://example.org/src-syms.tar"
      , "subdir": "repo"
      , "pragma": {"special": "resolve-completely"}
      }
    }
  , "main":
    { "repository":
      { "type": "distdir"
      , "repositories": ["unresolved", "resolved"]
      }
    }
  }
}
EOF

###
# Run the checks
##

# Compute present root locally from scratch (via distfile)
CONF=$("${JUST_MR}" --norc -C repos.json \
                    --just "${JUST}" \
                    --local-build-root "${LBR}" \
                    --distdir "${DISTDIR}" \
                    --log-limit 6 \
                    setup main)
cat "${CONF}"
echo
TREE=$(jq -r '.repositories.main.workspace_root[1]' "${CONF}")

# Remove the distdir
rm -rf "${DISTDIR}"

# Compute present root locally from stored file association
${JUST} gc --local-build-root ${LBR} 2>&1
${JUST} gc --local-build-root ${LBR} 2>&1

CONF=$("${JUST_MR}" --norc -C repos.json \
                    --just "${JUST}" \
                    --local-build-root "${LBR}" \
                    --log-limit 6 \
                    setup main)
cat "${CONF}"
echo
test $(jq -r '.repositories.main.workspace_root[1]' "${CONF}") = "${TREE}"

# We now test if the serve endpoint can provide us the present root.
# In a clean build root, ask serve to set up the root for us, from scratch
rm -rf "${LBR}"

CONF=$("${JUST_MR}" --norc -C repos.json \
                    --just "${JUST}" \
                    --local-build-root "${LBR}" \
                    --log-limit 6 \
                    ${ENDPOINT_ARGS} setup main)
cat "${CONF}"
echo
test $(jq -r '.repositories.main.workspace_root[1]' "${CONF}") = "${TREE}"

# Double-check the file association was created and root remains available
# without the remote endpoints
${JUST} gc --local-build-root ${LBR} 2>&1
${JUST} gc --local-build-root ${LBR} 2>&1

CONF=$("${JUST_MR}" --norc -C repos.json \
                    --just "${JUST}" \
                    --local-build-root "${LBR}" \
                    --log-limit 6 \
                    setup main)
cat "${CONF}"
echo
test $(jq -r '.repositories.main.workspace_root[1]' "${CONF}") = "${TREE}"

echo OK
