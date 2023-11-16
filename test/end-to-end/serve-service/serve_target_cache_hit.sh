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
# In this script, we first fill the target cache of a running just-serve
# instance by "locally" compiling the export target. Then, we check that
# just-serve can, well, serve the requested absent target.
#
# The remote properties and dispatch file are only used to test that both
# client and server shard the target cache entry in the same way
#
###########################################################################

set -eu
env
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"

readonly LBR="${TEST_TMPDIR}/local-build-root"

readonly LOCAL_DIR="${TEST_TMPDIR}/local"
readonly ABSENT_DIR="${TEST_TMPDIR}/absent"

readonly DISPATCH_FILE="${TEST_TMPDIR}/dispatch.json"

cat > "${DISPATCH_FILE}" <<EOF
[[{"runner": "node-name"}, "127.0.0.1:1234"]]
EOF

readonly REMOTE_PROPERTIES="--remote-execution-property foo:bar"
readonly DISPATCH="--endpoint-configuration ${DISPATCH_FILE}"

# Set up sample repository
readonly GENERATOR="${TEST_TMPDIR}/generate.sh"
readonly GEN_DIR="{TEST_TMPDIR}/gen-dir"
cat > "${GENERATOR}" <<EOF
#!/bin/sh

cat > TARGETS <<ENDTARGETS
{ "main-internal":
  { "type": "generic"
  , "cmds": ["echo hello from just-serve > out.txt"]
  , "outs": ["out.txt"]
  }
, "main":
  {"type": "export", "target": "main-internal", "flexible_config": ["ENV"]}
}
ENDTARGETS
EOF

cat "${GENERATOR}"

chmod 755 "${GENERATOR}"
mkdir -p "${GEN_DIR}"
( cd "${GEN_DIR}"
  git init
  git config user.email "nobody@example.org"
  git config user.name "Nobody"
  "${GENERATOR}"
  git add .
  git commit -m "first commit"
)
readonly TREE_ID=$(cd "${GEN_DIR}" && git log -n 1 --format="%T")

# fill the target cache that will be used by just serve
mkdir -p ${LOCAL_DIR}
( cd ${LOCAL_DIR}
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "git tree"
      , "id": "${TREE_ID}"
      , "cmd": ["${GENERATOR}"]
      }
    }
  }
}
EOF
echo "local repos configuration:"
cat repos.json
echo
CONF=$("${JUST_MR}" --norc --local-build-root "${SERVE_LBR}" setup)

echo "generated conf":
cat "${CONF}"
echo

"${JUST}" build                       \
    --local-build-root "${SERVE_LBR}" \
    -C "${CONF}"                      \
    -r "${REMOTE_EXECUTION_ADDRESS}"  \
    ${REMOTE_PROPERTIES}              \
    ${DISPATCH}                       \
    main
)

ls -R "${SERVE_LBR}"

# test with the absent repository
mkdir -p "${ABSENT_DIR}"
( cd "${ABSENT_DIR}"
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "git tree"
      , "id": "${TREE_ID}"
      , "cmd": ["${GENERATOR}"]
      , "pragma": {"absent": true}
      }
    }
  }
}
EOF
echo "absent repos configuration:"
cat repos.json
echo

rm "${GENERATOR}"

CONF=$("${JUST_MR}" --norc --local-build-root "${LBR}" setup)
cat "${CONF}"
echo
# test that it fails without using just serve
"${JUST}" analyse --local-build-root "${LBR}" -C "${CONF}" main && echo "this should fail" >&2 && exit 1
echo "failed as expected"

# test that it fails if we use a different remote execution address w.r.t. the
# one used by just-serve
"${JUST}" analyse                     \
    --local-build-root "${LBR}"       \
    --remote-serve-address "${SERVE}" \
    -C "${CONF}"                      \
    -r "${SERVE}"                     \
    ${REMOTE_PROPERTIES}              \
    ${DISPATCH}                       \
    main && echo "this should fail" >&2 && exit 1

# test that we can successfully compile using just serve
"${JUST}" build                       \
    --local-build-root "${LBR}"       \
    --remote-serve-address "${SERVE}" \
    -C "${CONF}"                      \
    -r "${REMOTE_EXECUTION_ADDRESS}"  \
    ${REMOTE_PROPERTIES}              \
    ${DISPATCH}                       \
    main
)
