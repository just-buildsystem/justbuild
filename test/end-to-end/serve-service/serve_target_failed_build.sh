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
readonly LBR_1="${TEST_TMPDIR}/local-build-root-1"
readonly LBR_2="${TEST_TMPDIR}/local-build-root-2"
readonly LBR_3="${TEST_TMPDIR}/local-build-root-3"
readonly OUTPUT_1="${TEST_TMPDIR}/output-dir-1"
readonly OUTPUT_2="${TEST_TMPDIR}/output-dir-2"
readonly DISTDIR="${TEST_TMPDIR}/distfiles"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

mkdir -p "${DISTDIR}"
cp src.tar "${DISTDIR}"
HASH=$(git hash-object src.tar)

readonly TARGETS_ROOT="${PWD}/data/targets"
readonly RULES_ROOT="${PWD}/data/rules"

mkdir work
cd work
touch ROOT
cat > repos.json <<EOF
{ "main": "main"
, "repositories":
  { "main":
    { "repository":
      { "type": "archive"
      , "content": "$HASH"
      , "fetch": "http://example.org/src.tar"
      , "subdir": "repo"
      }
    , "target_root": "target"
    , "rule_root": "rule"
    }
  , "rule":
    { "repository":
      {"type": "file", "path": "$RULES_ROOT", "pragma": {"to_git": true}}
    }
  , "target":
    { "repository":
      {"type": "file", "path": "$TARGETS_ROOT", "pragma": {"to_git": true}}
    }
  }
}
EOF

CONF=$("${JUST_MR}" --norc --local-build-root "${LBR_1}" \
                    --distdir ${DISTDIR} ${COMPAT} \
                    setup)
cat $CONF
echo

# Check that we can build locally correctly
${JUST} install --local-build-root "${LBR_1}" -C "${CONF}" \
                --log-limit 5 \
                -r "${REMOTE_EXECUTION_ADDRESS}" ${COMPAT} \
                -o "${OUTPUT_1}" 2>&1

for i in $(seq 5); do
  grep "./tree/src/$i.txt" ${OUTPUT_1}/_out
done
echo

# Check that build succeeds with a serve endpoint present
#
# Reason: serve endpoint does not have the correct targets and rules root and
# thus fails, but locally we can continue.

${JUST} install --local-build-root "${LBR_2}" -C "${CONF}" \
                --log-limit 5 \
                --remote-serve-address "${SERVE}" \
                -r "${REMOTE_EXECUTION_ADDRESS}" ${COMPAT} \
                -o "${OUTPUT_2}" 2>&1

for i in $(seq 5); do
  grep "./tree/src/$i.txt" ${OUTPUT_2}/_out
done
echo

# Check that build fails with a serve endpoint present if the orchestrated build
# actually fails
#
# Reason: the serve endpoint has all the roots to start the analysis/build of
# the target, but we use a dummy rule and thus build fails. This is reported to
# the client and in this case the whole build is expected to fail.

rm "${RULES_ROOT}/RULES"  # to match known rule tree on serve endpoint

cat > repos.json <<EOF
{ "main": "main"
, "repositories":
  { "main":
    { "repository":
      { "type": "archive"
      , "content": "$HASH"
      , "fetch": "http://example.org/src.tar"
      , "subdir": "repo"
      }
    , "target_root": "target"
    , "rule_root": "rule"
    , "rule_file_name": "RULES.dummy"
    }
  , "rule":
    { "repository":
      {"type": "file", "path": "$RULES_ROOT", "pragma": {"to_git": true}}
    }
  , "target":
    { "repository":
      {"type": "file", "path": "$TARGETS_ROOT", "pragma": {"to_git": true}}
    }
  }
}
EOF

CONF=$("${JUST_MR}" --norc --local-build-root "${LBR_3}" \
                    --distdir ${DISTDIR} ${COMPAT} \
                    setup)
cat $CONF
echo

${JUST} analyse --local-build-root "${LBR_3}" -C "${CONF}" \
                --log-limit 5 \
                --remote-serve-address "${SERVE}" \
                -r "${REMOTE_EXECUTION_ADDRESS}" ${COMPAT} 2>&1 && exit 1 || :
echo Failed as expected

echo OK
