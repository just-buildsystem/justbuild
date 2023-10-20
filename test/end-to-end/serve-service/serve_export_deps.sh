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
# We test that we are able to compile an export target, which depends on an
# absent one.
#
###########################################################################

set -eu
env
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"

readonly LBR="${TEST_TMPDIR}/local-build-root"

readonly LOCAL_DIR="${TEST_TMPDIR}/local"
readonly ABSENT_DIR="${TEST_TMPDIR}/absent"

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

CONF=$("${JUST_MR}" --norc --local-build-root "${SERVE_LBR}" setup)
cat "${CONF}"
"${JUST}" build --local-build-root "${SERVE_LBR}" -C "${CONF}" -r ${REMOTE_EXECUTION_ADDRESS} main
)

# Set up local repository
readonly GENERATOR_LOCAL="${TEST_TMPDIR}/generate_local.sh"
readonly GEN_DIR_LOCAL="{TEST_TMPDIR}/local-repo"
cat > "${GENERATOR_LOCAL}" <<EOF
#!/bin/sh

cat > TARGETS <<EOFTARGETS
{ "main-internal":
  { "type": "generic"
  , "cmds": ["cat out.txt > local.txt"]
  , "outs": ["local.txt"]
  , "deps" : [["@", "absent-dep", "", "main"]]
  }
, "main":
  {"type": "export", "target": "main-internal", "flexible_config": ["ENV"]}
}
EOFTARGETS
EOF

chmod 755 "${GENERATOR_LOCAL}"
mkdir -p "${GEN_DIR_LOCAL}"
( cd "${GEN_DIR_LOCAL}"
  git init
  git config user.email "nobody@example.org"
  git config user.name "Nobody"
  "${GENERATOR_LOCAL}"
  git add .
  git commit -m "first commit"
)
readonly TREE_ID_LOCAL=$(cd "${GEN_DIR_LOCAL}" && git log -n 1 --format="%T")


# test with the absent repository
mkdir -p "${ABSENT_DIR}"
( cd "${ABSENT_DIR}"
touch ROOT
cat > repos.json <<EOF
{
  "repositories":
  { "local":
    { "repository":
      { "type": "git tree"
      , "id": "${TREE_ID_LOCAL}"
      , "cmd": ["${GENERATOR_LOCAL}"]
      }
    , "bindings" : {"absent-dep": "absent-dep"}
    }
  , "absent-dep":
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

CONF=$("${JUST_MR}" --norc --local-build-root "${LBR}" setup --all)
cat "${CONF}"
echo

# test that we can successfully compile using just serve
"${JUST}" build --main local --local-build-root "${LBR}" -C "${CONF}" --remote-serve-address ${SERVE} -r ${REMOTE_EXECUTION_ADDRESS} main
"${JUST}" build --main local --local-build-root "${LBR}" -C "${CONF}" --remote-serve-address ${SERVE} -r ${REMOTE_EXECUTION_ADDRESS} main
)
