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

set -eu

JUST_MR="$(pwd)/bin/mr-tool-under-test"
JUST="$(pwd)/bin/tool-under-test"
LBR="${TEST_TMPDIR}/local-build-root"
LBR2="${TEST_TMPDIR}/new-local-build-root"
OUT="${TEST_TMPDIR}/out"

mkdir -p bin
cat > bin/mock-vcs <<'EOF'
#!/bin/sh
if [ "$(cat ${CREDENTIAL_PATH:-/dev/null})" = "sEcReT" ]
then
  echo 'checked-out sources' > sources.txt
  echo '{}' > TARGETS
else
  echo 'not enough credentials available'
fi
EOF
chmod 755 bin/mock-vcs
export PATH="$(pwd)/bin:${PATH}"

mkdir -p etc
echo -n sEcReT > etc/pass
export CREDENTIAL_PATH="$(pwd)/etc/pass"

# Compute tree of our mock checkout
mkdir work
( cd work
  mock-vcs 2>&1
  git init 2>&1
  git config user.email "nobody@example.org" 2>&1
  git config user.name "Nobody" 2>&1
  git add . 2>&1
  git commit -m "Sample output" 2>&1
)
readonly TREE_ID=$(cd work && git log -n 1 --format="%T")
rm -rf work
echo "Tree of checkout is ${TREE_ID}."

# Setup repo config
mkdir repo
cd repo
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "git tree"
      , "id": "${TREE_ID}"
      , "cmd": ["mock-vcs"]
      , "inherit env": ["PATH", "CREDENTIAL_PATH"]
      }
    }
  }
}
EOF
cat repos.json

# Succesfull build

"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" \
             install -o "${OUT}" '' sources.txt 2>&1
grep checked-out "${OUT}/sources.txt"

# Verify the environment is needed
export CREDENTIALS_PATH=/dev/null
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR2}" \
             build 2>&1 && exit 1 || :

echo DONE
