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

readonly JUST="${PWD}/bin/tool-under-test"
# Local
readonly LOCAL_ROOT="${TMPDIR}/local"
readonly LOCAL_CACHE="${LOCAL_ROOT}/build-root"
# Remote 1
readonly R1_ROOT=$(readlink -f "${PWD}/../remote")
readonly R1_CACHE="${R1_ROOT}/build-root"
readonly R1_ADDRESS="${REMOTE_EXECUTION_ADDRESS}"
# Remote 2
readonly R2_ROOT="${TMPDIR}/remote"
readonly R2_CACHE="${R2_ROOT}/build-root"
readonly R2_INFOFILE="${R2_ROOT}/info.json"
readonly R2_PIDFILE="${R2_ROOT}/pid.txt"
readonly R2_BIN_DIR="${R2_ROOT}/bin"

COMPATIBLE_ARGS=""
if [ -n "${COMPATIBLE:-}" ]; then
  COMPATIBLE_ARGS="--compatible"
fi

# This test tests the round trip of large objects being split at one
# remote execution, being spliced at another remote execution, and being
# transferred back to the host.
#
# This test requires two remote-execution servers. One is given
# implicitly by the test infrastructure, another one is explicitly
# started.
#
# In order to verify the round trip, we add a magic word into the
# objects created at remote execution 1, make a transformation to upper
# case at remote execution 2 using a special local launcher, and grep
# for the modified magic word at the local host.

mkdir -p "${R2_BIN_DIR}"
cat > "${R2_BIN_DIR}/toupper" <<'EOF'
#!/bin/sh
tr 'a-z' 'A-Z' < "$1"
EOF
chmod 755 "${R2_BIN_DIR}/toupper"

echo
R2_LAUNCHER='["env", "PATH='"${R2_BIN_DIR}:${PATH}"'"]'
echo "Remote-execution 2 will use ${R2_LAUNCHER} as local launcher"
echo

"${JUST}" execute --info-file "${R2_INFOFILE}" --pid-file "${R2_PIDFILE}" \
          --local-build-root "${R2_CACHE}" -L "${R2_LAUNCHER}" \
          ${COMPATIBLE_ARGS} 2>&1 &

for _ in `seq 1 60`; do
  if test -f "${R2_INFOFILE}"; then
    break
  fi
  sleep 1;
done

if ! test -f "${R2_INFOFILE}"; then
  echo "Did not find ${R2_INFOFILE}"
  exit 1
fi

cleanup() {
  kill $(cat "${R2_PIDFILE}")
}
trap cleanup EXIT

readonly R2_PORT=$(jq '."port"' "${R2_INFOFILE}")
readonly R2_ADDRESS="127.0.0.1:${R2_PORT}"
echo
echo "Remote-execution 1: ${R1_ADDRESS}"
echo "Remote-execution 2: ${R2_ADDRESS}"
echo

# A file and a tree > 4 MB each are created at remote execution 1, sent
# to the remote execution 2 via a dispatch action, enforcing splitting
# at remote execution 1 and splicing at remote-execution 2. The final
# result is sent back to the host.

cat > TARGETS <<'EOF'
{ "file":
  { "type": "generic"
  , "outs": ["file.txt"]
  , "cmds":
    [ "for i in $(seq 21846); do printf '%0192d\n' $i >> file.txt; done"
    , "echo magic_word >> file.txt"
    ]
  }
, "tree":
  { "type": "generic"
  , "out_dirs": ["tree"]
  , "cmds":
    [ "for i in $(seq 21846); do echo foo > tree/$(printf '%0192d' $i).txt; done"
    , "echo foo > tree/magic_word.txt"
    ]
  }
, "dispatch":
  { "type": "generic"
  , "cmds":
    [ "toupper file.txt > file_out.txt"
    , "ls tree > tree.txt"
    , "toupper tree.txt > tree_out.txt"
    ]
  , "outs": ["file_out.txt", "tree_out.txt"]
  , "execution properties":
    {"type": "singleton_map", "key": "server", "value": "special"}
  , "deps": ["file", "tree"]
  }
, "":
  { "type": "generic"
  , "cmds": ["cat file_out.txt tree_out.txt > final.txt"]
  , "deps": ["dispatch"]
  , "outs": ["final.txt"]
  }
}
EOF

cat > dispatch.json <<EOF
[ [ {"server": "special"}, "${R2_ADDRESS}"] ]
EOF

touch ROOT

"${JUST}" install -o . --local-build-root "${LOCAL_CACHE}" -r "${R1_ADDRESS}" \
          --endpoint-configuration dispatch.json ${COMPATIBLE_ARGS} 2>&1

[ "$(grep -c MAGIC_WORD final.txt)" = 2 ]

echo OK
