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
# This test checks if we can make a present root for a Git repository using the
# serve endpoint.
##

set -eu

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

ENDPOINT_ARGS="-r ${REMOTE_EXECUTION_ADDRESS} -R ${SERVE} ${COMPAT}"

###
# Setup sample repos config
##

mkdir work
cd work

touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "main":
    { "repository":
      { "type": "git"
      , "commit": "$COMMIT_0"
      , "repository": "http://non-existent.example.org/data.git"
      , "branch": "master"
      , "subdir": "."
      }
    }
  }
}
EOF

###
# Run the checks
##

# Compute present root by asking the serve endpoint to set it up for us. This
# requires remotes in native mode.
if [ -z "${COMPAT}" ]; then

  CONF=$("${JUST_MR}" --norc -C repos.json \
                      --just "${JUST}" \
                      --local-build-root "${LBR}" \
                      --log-limit 6 \
                      ${ENDPOINT_ARGS} setup main)
  cat "${CONF}"
  echo
  test $(jq -r '.repositories.main.workspace_root[1]' "${CONF}") = "${TREE_0}"

  # Compute present root locally from now populated Git cache
  ${JUST} gc --local-build-root ${LBR} 2>&1
  ${JUST} gc --local-build-root ${LBR} 2>&1

  CONF=$("${JUST_MR}" --norc -C repos.json \
                      --just "${JUST}" \
                      --local-build-root "${LBR}" \
                      --log-limit 6 \
                      setup main)
  cat "${CONF}"
  echo
  test $(jq -r '.repositories.main.workspace_root[1]' "${CONF}") = "${TREE_0}"

  # Check that the subdir is also working correctly
  ${JUST} gc --local-build-root ${LBR} 2>&1
  ${JUST} gc --local-build-root ${LBR} 2>&1

  cat > repos.json <<EOF
{ "repositories":
  { "main":
    { "repository":
      { "type": "git"
      , "commit": "$COMMIT_0"
      , "repository": "http://non-existent.example.org/data.git"
      , "branch": "master"
      , "subdir": "repo"
      }
    }
  }
}
EOF

  CONF=$("${JUST_MR}" --norc -C repos.json \
                      --just "${JUST}" \
                      --local-build-root "${LBR}" \
                      --log-limit 6 \
                      setup main)
  cat "${CONF}"
  echo

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
