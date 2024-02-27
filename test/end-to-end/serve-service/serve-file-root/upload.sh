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
# This test checks that the root for a file repository marked to_git is being
# uploaded to the serve endpoint.
#
# Also checks the upload of locally known absent roots for Git-tree
# repositories.
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
# Setup sample repos config with present and absent repos
##

mkdir -p "${LOCAL_REPO}"
tar xf src.tar -C "${LOCAL_REPO}"

cd "${LOCAL_REPO}"
git init -b trunk
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git add -f .
git commit -m 'Test repo' 2>&1
TREE=$(git log -n 1 --format="%T")
cd -

mkdir work
cd work

touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "present_file":
    { "repository":
      { "type": "file"
      , "path": "${LOCAL_REPO}"
      , "pragma": {"to_git": true}
      }
    }
  , "absent_git_tree":
    { "repository":
      { "type": "git tree"
      , "id": "${TREE}"
      , "cmd": ["non_existent_script.sh"]
      , "pragma": {"absent": true}
      }
    }
  }
}
EOF

###
# Run the checks
##

# Setup an absent root from local path. Even if root is present, if a serve
# endpoint is given then we try to set it up there as well. As this serve
# endpoint does not know the tree, it will try to upload through the remote CAS.
# The upload succeeds if remote in native mode, but fails (non-fatally) in
# compatible mode.
CONF=$("${JUST_MR}" --norc -C repos.json \
                    --just "${JUST}" \
                    --local-build-root "${LBR}" \
                    --log-limit 6 \
                    ${ENDPOINT_ARGS} setup present_file)
cat "${CONF}"
echo
test $(jq -r '.repositories.present_file.workspace_root[1]' "${CONF}") = "${TREE}"

# Check in a clean local build root that the serve endpoint now has the root
# tree. This can only work in native mode, where the root was actually uploaded.
if [ -z "${COMPAT}" ]; then

  rm -rf "${LBR}"

  CONF=$("${JUST_MR}" --norc -C repos.json \
                      --just "${JUST}" \
                      --local-build-root "${LBR}" \
                      --log-limit 6 \
                      ${ENDPOINT_ARGS} setup absent_git_tree)
  cat "${CONF}"
  echo

else

  echo ---
  echo Checking expected failures

  rm -rf "${LBR}"
  "${JUST_MR}" --norc -C repos.json \
               --just "${JUST}" \
               --local-build-root "${LBR}" \
               --log-limit 6 \
               ${ENDPOINT_ARGS} setup absent_git_tree 2>&1 && exit 1 || :
  echo Failed as expected
fi

echo OK
