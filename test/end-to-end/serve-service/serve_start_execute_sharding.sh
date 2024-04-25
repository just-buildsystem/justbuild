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
# In this script, we first fill the target cache of a just-serve instance
# by building an export target. Then, we check that just-serve can get a
# cache hit if set up to act also as just-execute.
#
# The remote properties and dispatch file are used to test that both
# client and server shard the target cache entry in the same way.
#
###########################################################################

set -eu
env

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"

readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly SERVE_LBR="${TEST_TMPDIR}/serve-local-build-root"

readonly LOCAL_DIR="${TEST_TMPDIR}/local"
readonly ABSENT_DIR="${TEST_TMPDIR}/absent"

readonly INFOFILE="${PWD}/info.json"
readonly PIDFILE="${PWD}/pid.txt"

##
# Set up sample repository
#
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

##
# Fill the target cache that will be used by just serve by building locally
#
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
CONF=$("${JUST_MR}" -L '["env", "PATH='"${PATH}"'"]' --norc --local-build-root "${SERVE_LBR}" setup)

echo "generated conf":
cat "${CONF}"
echo

"${JUST}" build                         \
    --local-build-root "${SERVE_LBR}"   \
    -L '["env", "PATH='"${PATH}"'"]'    \
    -C "${CONF}"                        \
    main
)

ls -R "${SERVE_LBR}"

##
# Set up a just serve instance that acts also as just execute, using the
# build root with pre-filled target cache
#
cat > .just-servec <<EOF
{ "repositories": []
, "remote service":
  { "info file": {"root": "system", "path": "${INFOFILE}"}
  , "pid file": {"root": "system", "path": "${PIDFILE}"}
  }
, "local build root": {"root": "system", "path": "${SERVE_LBR}"}
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

##
# Change repository to absent and unbuildable (remove generator script) and
# test that we can build from the populated build root, as we should get a
# target cache hit, assuming sharding is done as for a local build
#
mkdir -p "${ABSENT_DIR}"
( cd "${ABSENT_DIR}"
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "git tree"
      , "id": "${TREE_ID}"
      , "cmd": ["non_existent_script.sh"]
      , "pragma": {"absent": true}
      }
    }
  }
}
EOF
echo "absent repos configuration:"
cat repos.json
echo

CONF=$("${JUST_MR}" -L '["env", "PATH='"${PATH}"'"]' --norc --local-build-root "${LBR}" setup)
cat "${CONF}"
echo

"${JUST}" build                                \
    --local-build-root "${LBR}"                \
    --remote-serve-address 127.0.0.1:${PORT}   \
    -L '["env", "PATH='"${PATH}"'"]'    \
    -C "${CONF}"                               \
    main
)

cleanup
echo ok
