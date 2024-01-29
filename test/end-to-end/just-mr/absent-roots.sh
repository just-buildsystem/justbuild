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

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"

readonly INFOFILE="${PWD}/info.json"
readonly PIDFILE="${PWD}/pid.txt"

readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly SERVE_LBR="${TEST_TMPDIR}/serve-local-build-root"

readonly REPO_ROOT="${PWD}/serve_repo"
readonly TEST_DATA="The content of the data file in foo"
readonly LINK_TARGET="dummy"

# Set up sample repository
mkdir -p "${REPO_ROOT}/foo/bar"
echo -n "${TEST_DATA}" > "${REPO_ROOT}/foo/bar/data.txt"
ln -s "${LINK_TARGET}" "${REPO_ROOT}/foo/bar/link"
{
  cd "${REPO_ROOT}"
  git init
  git config user.name 'N.O.Body'
  git config user.email 'nobody@example.org'
  git add -f .
  git commit -m 'Test repo' 2>&1
}
readonly GIT_COMMIT_ID=$(cd "${REPO_ROOT}" && git log -n 1 --format='%H')
readonly GIT_ROOT_ID=$(cd "${REPO_ROOT}" && git log -n 1 --format='%T')

# Setup sample repository config
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "test":
    { "repository":
      { "type": "git"
      , "repository": "http://non-existent.example.org/dummy.git"
      , "commit": "${GIT_COMMIT_ID}"
      , "branch": "main"
      , "subdir": "."
      , "pragma": {"absent": true}
      }
    }
  }
}
EOF
echo "Repository configuration:"
cat repos.json

# Make the repository available via 'just serve'
cat > .just-servec <<EOF
{ "repositories": ["${REPO_ROOT}"]
, "remote service": {"info file": "${INFOFILE}", "pid file": "${PIDFILE}"}
, "local build root": "${SERVE_LBR}"
}
EOF
echo "Serve service configuration:"
cat .just-servec

"${JUST}" serve .just-servec 2>&1 &

for _ in `seq 1 60`
do
    if test -f "${INFOFILE}"
    then
        break
    fi
    sleep 1;
done

if ! test -f "${INFOFILE}"
then
    echo "Did not find ${INFOFILE}"
    exit 1
fi

readonly PORT=$(jq '."port"' "${INFOFILE}")

cleanup() {
  kill $(cat "${PIDFILE}")
}
trap cleanup EXIT

# Compute the repository configuration
CONF=$("${JUST_MR}" --norc --local-build-root "${LBR}" --remote-serve-address localhost:${PORT} setup)
cat "${CONF}"
echo

# Check that an absent root was created
test "$(jq -r '.repositories.test.workspace_root[0]' "${CONF}")" = "git tree"
test $(jq -r '.repositories.test.workspace_root | length' "${CONF}") = 2

# Check the tree was correctly set via 'just serve' remote
test "$(jq -r '.repositories.test.workspace_root[1]' "${CONF}")" = "${GIT_ROOT_ID}"

# Now check that an absent git tree repository works without running the command
rm -rf "${LBR}"

cat > repos.json <<EOF
{ "repositories":
  { "test":
    { "repository":
      { "type": "git tree"
      , "id": "${GIT_ROOT_ID}"
      , "cmd": ["non_existent_script.sh"]
      , "pragma": {"absent": true}
      }
    }
  }
}
EOF
echo "Repository configuration:"
cat repos.json

# Compute the repository configuration
CONF=$("${JUST_MR}" --norc --local-build-root "${LBR}" --remote-serve-address localhost:${PORT} setup)
cat "${CONF}"
echo

# Check that an absent root was created
test "$(jq -r '.repositories.test.workspace_root[0]' "${CONF}")" = "git tree"
test $(jq -r '.repositories.test.workspace_root | length' "${CONF}") = 2

# Check the tree was correctly set via 'just serve' remote
test "$(jq -r '.repositories.test.workspace_root[1]' "${CONF}")" = "${GIT_ROOT_ID}"

echo OK
