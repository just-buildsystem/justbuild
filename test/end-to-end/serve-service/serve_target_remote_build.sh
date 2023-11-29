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
readonly OUTPUT="${TEST_TMPDIR}/output-dir"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

mkdir work
cd work
touch ROOT
cat > repos.json <<EOF
{ "main": "main"
, "repositories":
  { "main":
    { "repository":
      { "type": "git"
      , "commit": "$COMMIT_0"
      , "pragma": {"absent": true}
      , "repository": "http://non-existent.example.org/data.git"
      , "branch": "master"
      , "subdir": "."
      }
    , "target_root": "targets"
    , "rule_root": "rules"
    }
  , "rules":
    { "repository":
      { "type": "git"
      , "commit": "$COMMIT_1"
      , "pragma": {"absent": true}
      , "repository": "http://non-existent.example.org/data.git"
      , "branch": "master"
      , "subdir": "test/end-to-end/serve-service/data/rules"
      }
    }
  , "targets":
    { "repository":
      { "type": "git"
      , "commit": "$COMMIT_1"
      , "pragma": {"absent": true}
      , "repository": "http://non-existent.example.org/data.git"
      , "branch": "master"
      , "subdir": "test/end-to-end/serve-service/data/targets"
      }
    }
  }
}
EOF

CONF=$("${JUST_MR}" --norc --local-build-root "${LBR}" \
                    --remote-serve-address ${SERVE} \
                    -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
                    setup)
cat $CONF

# Check that we can build correctly
${JUST} install --local-build-root "${LBR}" -C "${CONF}" \
                --remote-serve-address ${SERVE} \
                --log-limit 6 \
                -r "${REMOTE_EXECUTION_ADDRESS}" ${COMPAT} \
                -o "${OUTPUT}" 2>&1

for i in $(seq 5); do
  grep "./tree/src/$i.txt" ${OUTPUT}/_out
done

echo OK
