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
#
# The test archive contains symlinks to be resolved, which tests also the
# resolved tree file association.
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
HASH=$(git hash-object src.tar)

mkdir work
cd work

touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "present":
    { "repository":
      { "type": "archive"
      , "content": "$HASH"
      , "fetch": "http://example.org/src.tar"
      , "subdir": "repo"
      , "pragma": {"special": "resolve-completely"}
      }
    }
  , "absent":
    { "repository":
      { "type": "archive"
      , "content": "$HASH"
      , "fetch": "http://example.org/src.tar"
      , "subdir": "repo"
      , "pragma": {"special": "resolve-completely", "absent": true}
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
                    setup present)
cat "${CONF}"
echo
TREE=$(jq -r '.repositories.present.workspace_root[1]' "${CONF}")

# Remove the distdir
rm -rf "${DISTDIR}"

# Compute absent root by asking serve to set it up from scratch. This works also
# in compatible mode, as the serve endpoint has the archive in Git cache.
rm -rf "${LBR}"

CONF=$("${JUST_MR}" --norc -C repos.json \
                    --just "${JUST}" \
                    --local-build-root "${LBR}" \
                    --log-limit 6 \
                    ${ENDPOINT_ARGS} setup absent)
cat "${CONF}"
echo
test $(jq -r '.repositories.absent.workspace_root[1]' "${CONF}") = "${TREE}"

# Check that serve can provide this tree as present in a clean build root. This
# can happen however only in native mode.
if [ -z "${COMPAT}" ]; then

  rm -rf "${LBR}"
  CONF=$("${JUST_MR}" --norc -C repos.json \
                      --just "${JUST}" \
                      --local-build-root "${LBR}" \
                      --log-limit 6 \
                      ${ENDPOINT_ARGS} setup present)
  cat "${CONF}"
  echo
  test $(jq -r '.repositories.present.workspace_root[1]' "${CONF}") = "${TREE}"

else

  echo ---
  echo Checking expected failures

  rm -rf "${LBR}"
  "${JUST_MR}" --norc -C repos.json \
               --just "${JUST}" \
               --local-build-root "${LBR}" \
               --log-limit 6 \
               ${ENDPOINT_ARGS} setup present 2>&1 && exit 1 || :
  echo Failed as expected
fi

echo OK
