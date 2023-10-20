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

###########################################################################
#
# This script aims to test the "remote build capabilities" of a just-serve
# instance.
#
###########################################################################

set -eu

env
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"

mkdir work
cd work
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "git"
      , "commit": "$COMMIT_0"
      , "pragma": {"absent": true}
      , "repository": "http://non-existent.example.org/data.git"
      , "branch": "master"
      , "subdir": "greetlib/greet"
      }
    }
  }
}
EOF

CONF=$("${JUST_MR}" --norc --local-build-root "${LBR}" \
                    --remote-serve-address ${SERVE} \
                    -r ${REMOTE_EXECUTION_ADDRESS} \
                    setup)
cat $CONF

# this test is expected to fail until the just serve implements orchestration of
# remote build
${JUST} build --local-build-root "${LBR}" -C "${CONF}" \
          --remote-serve-address ${SERVE} \
          --log-limit 8 \
          -r "${REMOTE_EXECUTION_ADDRESS}" greet 2>&1 && \
          echo "This test should fail" && exit 1

echo OK
