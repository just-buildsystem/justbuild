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
# This test checks that an absent root can successfully be made by asking
# the serve endpoint to set it up for us when we have no local knowledge.
##

set -eu

env

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

ENDPOINT_ARGS="-r ${REMOTE_EXECUTION_ADDRESS} -R ${SERVE} ${COMPAT}"

###
# Setup sample repos config with present and absent repos
##

mkdir work
cd work

touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "main":
    { "repository":
      { "type": "git tree"
      , "id": "$TREE_0"
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

# Compute absent root by asking serve to set it up from scratch.
rm -rf "${LBR}"

CONF=$("${JUST_MR}" --norc -C repos.json \
                    --just "${JUST}" \
                    --local-build-root "${LBR}" \
                    --log-limit 6 \
                    ${ENDPOINT_ARGS} setup main)
cat "${CONF}"
echo
test $(jq -r '.repositories.main.workspace_root[1]' "${CONF}") = "${TREE_0}"

echo OK
