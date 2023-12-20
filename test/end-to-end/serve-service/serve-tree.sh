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

set -e

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR_DEBUG="${TEST_TMPDIR}/build-root-DEBUG"
readonly LBR_A="${TEST_TMPDIR}/build-root-A"
readonly LBR_B="${TEST_TMPDIR}/build-root-B"
readonly LBR_C="${TEST_TMPDIR}/build-root-C"
readonly LBR_D="${TEST_TMPDIR}/build-root-D"
readonly DISTDIR="${TEST_TMPDIR}/distfiles"

readonly TARGET_ROOT="${PWD}/data/targets"

mkdir -p "${DISTDIR}"
cp src.tar "${DISTDIR}"
HASH=$(git hash-object src.tar)

REMOTE="-r ${REMOTE_EXECUTION_ADDRESS}"

mkdir work
cd work

touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "archive"
      , "content": "$HASH"
      , "fetch": "http://example.org/src.tar"
      , "subdir": "repo"
      }
    , "target_root": "targets"
    , "target_file_name": "TARGETS.tree"
    }
  , "targets":
    { "repository":
      {"type": "file", "path": "${TARGET_ROOT}", "pragma": {"to_git": true}}
    }
  }
}
EOF

echo
echo MR configuration
cat repos.json
echo
echo Resulting just repo configuraiton
cat $("${JUST_MR}" --norc --local-build-root "${LBR_DEBUG}" \
                   --distdir "${DISTDIR}" setup)
echo

echo
echo Local build
"${JUST_MR}" --norc --local-build-root "${LBR_A}" --just "${JUST}" \
             --distdir "${DISTDIR}" build \
             --log-limit 4 \
             --dump-artifacts local.json 2>&1

echo
echo Remote build
"${JUST_MR}" --norc --local-build-root "${LBR_B}" --just "${JUST}" \
             --distdir "${DISTDIR}" ${REMOTE} build \
             --log-limit 4 \
             --dump-artifacts remote.json 2>&1

echo
echo Serve build
"${JUST_MR}" --norc --local-build-root "${LBR_C}" --just "${JUST}" \
             ${REMOTE} -R ${SERVE} build \
             --log-limit 4 \
             --dump-artifacts serve.json 2>&1

echo
echo Absent build
echo -n '[""]' > abs
"${JUST_MR}" --norc --local-build-root "${LBR_D}" --just "${JUST}" \
             --distdir "${DISTDIR}" ${REMOTE} -R ${SERVE} --absent abs build \
             --log-limit 4 \
             --dump-artifacts absent.json 2>&1

diff -u local.json remote.json
diff -u local.json serve.json
diff -u local.json absent.json

echo OK
