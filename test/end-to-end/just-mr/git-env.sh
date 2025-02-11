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

set -eu

JUST_MR="$(pwd)/bin/mr-tool-under-test"
JUST="$(pwd)/bin/tool-under-test"
WRKDIR="$(pwd)/work"
MOCK_TOOLS="$(pwd)/mock-bin"
GIT_REPO="${TEST_TMPDIR}/repo"
LBR="${TEST_TMPDIR}/local-build-root"
LBR2="${TEST_TMPDIR}/local-build-root-2"
OUT="${TEST_TMPDIR}/out"

# create git repo
mkdir -p "${GIT_REPO}"
cd "${GIT_REPO}"
git init 2>&1
git branch -m stable-1.0 2>&1
echo 'checked-out sources' > sources.txt
echo '{}' > TARGETS
git config user.email "nobody@example.org" 2>&1
git config user.name "Nobody" 2>&1
git add . 2>&1
git commit -m "Sample output" 2>&1
git show
COMMIT=$(git log -n 1 --format="%H")
echo "Created commit ${COMMIT}"

# create mock version of git
mkdir -p "${MOCK_TOOLS}"
MOCK_GIT="${MOCK_TOOLS}/mock-git"
cat > "${MOCK_GIT}" <<'EOF'
#!/bin/sh
if [ "$(cat ${CREDENTIAL_PATH:-/dev/null})" = "sEcReT" ] && [ "$(cat ${LOCAL_CREDENTIAL_PATH:-/dev/null})" = "local-SeCrEt" ]
then
EOF
cat >> "${MOCK_GIT}" <<EOF
  git fetch ${GIT_REPO} stable-1.0
EOF
cat >> "${MOCK_GIT}" <<'EOF'
else
  echo 'not enough credentials available'
  exit 1
fi
EOF
chmod 755 "${MOCK_GIT}"
echo
cat "${MOCK_GIT}"
echo

# Set up client root
mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
mkdir -p etc
echo -n sEcReT > etc/pass
export CREDENTIAL_PATH="$(pwd)/etc/pass"
echo -n local-SeCrEt > etc/local-pass
export LOCAL_CREDENTIAL_PATH="$(pwd)/etc/local-pass"

mkdir repo
cd repo
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "git"
      , "commit": "${COMMIT}"
      , "repository": "protocolcertainlynotknowntojust://git@example.org/repo"
      , "branch": "stable-1.0"
      , "inherit env": ["PATH", "CREDENTIAL_PATH"]
      }
    }
  }
}
EOF
echo
cat repos.json
echo

cat > local.json <<'EOF'
{"extra inherit env": ["LOCAL_CREDENTIAL_PATH"]}
EOF

# Succesfull build

"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" \
             --checkout-locations local.json --git "${MOCK_GIT}" --log-limit 5 \
             install -o "${OUT}" '' sources.txt 2>&1
grep checked-out "${OUT}/sources.txt"

# Verify the local.json is needed
"${JUST_MR}" --norc --git "${MOCK_GIT}" --local-build-root "${LBR2}" setup 2>&1 && exit 1 || :

# Verify the environment is needed
export CREDENTIAL_PATH=/dev/null
"${JUST_MR}" --norc --git "${MOCK_GIT}" --local-build-root "${LBR2}" --checkout-locations local.json setup 2>&1 && exit 1 || :

echo DONE
