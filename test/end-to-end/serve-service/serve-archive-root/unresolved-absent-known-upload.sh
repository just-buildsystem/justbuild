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
# This test checks that an absent root can successfully be made in the presence
# of the serve endpoint in the situation where we already have the file
# association (i.e., we know the unresolved root tree) and the serve endpoint
# does not know the archive. The upload can only happen in native mode.
#
# The test archive does not contain symlinks.
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
      }
    }
  , "absent":
    { "repository":
      { "type": "archive"
      , "content": "$HASH"
      , "fetch": "http://example.org/src.tar"
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

# While keeping the file association, ask serve endpoint to provide the root as
# absent. For a serve endpoint that does not have the archive blob available,
# this will require uploading the locally-known root tree to remote CAS, from
# where the serve endpoint will pick it up. This can only happen in native mode.
if [ -z "${COMPAT}" ]; then

  ${JUST} gc --local-build-root ${LBR} 2>&1
  ${JUST} gc --local-build-root ${LBR} 2>&1

  CONF=$("${JUST_MR}" --norc -C repos.json \
                      --just "${JUST}" \
                      --local-build-root "${LBR}" \
                      --log-limit 6 \
                      ${ENDPOINT_ARGS} setup absent)
  cat "${CONF}"
  echo
  test $(jq -r '.repositories.absent.workspace_root[1]' "${CONF}") = "${TREE}"

else

  echo ---
  echo Checking expected failures

  ${JUST} gc --local-build-root ${LBR} 2>&1
  ${JUST} gc --local-build-root ${LBR} 2>&1

  "${JUST_MR}" --norc -C repos.json \
               --just "${JUST}" \
               --local-build-root "${LBR}" \
               --log-limit 6 \
               ${ENDPOINT_ARGS} setup absent 2>&1 && exit 1 || :
  echo Failed as expected
fi

echo OK
