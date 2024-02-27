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
# This test checks that an absent root known in a local checkout can be
# successfully uploaded to a serve endpoint. This can only succeed in native
# mode.
##

set -eu

env

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LOCAL_REPO="${TEST_TMPDIR}/repo_root"
readonly LBR="${TEST_TMPDIR}/local-build-root"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

ENDPOINT_ARGS="-r ${REMOTE_EXECUTION_ADDRESS} -R ${SERVE} ${COMPAT}"

###
# Setup sample repos config
##

mkdir -p "${LOCAL_REPO}"
tar xf src.tar -C "${LOCAL_REPO}"

cd "${LOCAL_REPO}"
git init -b trunk
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git add -f .
git commit -m 'Test repo' 2>&1
COMMIT=$(git log -n 1 --format="%H")
SUBTREE=$(git ls-tree "$COMMIT" repo | awk '{print $3}')
cd -

mkdir work
cd work

touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "main":
    { "repository":
      { "type": "git"
      , "commit": "$COMMIT"
      , "repository": "${LOCAL_REPO}"
      , "branch": "trunk"
      , "subdir": "repo"
      , "pragma": {"absent": true}
      }
    }
  }
}
EOF

###
# Run the checks
##

# Setup an absent root from a local checkout. For a serve endpoint that does
# not have the commit available, this will upload the locally-known root tree
# to remote CAS, from where the serve endpoint will pick it up. This requires
# that the remotes are in native mode.
if [ -z "${COMPAT}" ]; then

  CONF=$("${JUST_MR}" --norc -C repos.json \
                      --just "${JUST}" \
                      --local-build-root "${LBR}" \
                      --log-limit 6 \
                      ${ENDPOINT_ARGS} setup main)
  cat "${CONF}"
  echo
  test $(jq -r '.repositories.main.workspace_root[1]' "${CONF}") = "${SUBTREE}"

else

  echo ---
  echo Checking expected failures

  "${JUST_MR}" --norc -C repos.json \
               --just "${JUST}" \
               --local-build-root "${LBR}" \
               --log-limit 6 \
               ${ENDPOINT_ARGS} setup main 2>&1 && exit 1 || :
  echo Failed as expected
fi

echo OK
