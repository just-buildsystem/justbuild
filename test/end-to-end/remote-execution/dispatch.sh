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
readonly LBRDIR="${PWD}/local-build-root"
readonly ESDIR="${PWD}/service-build-root"
readonly INFOFILE="${PWD}/info.json"
readonly PIDFILE="${PWD}/pid.txt"

# A standard remote-execution server is given by the test infrastructure
REMOTE_EXECUTION_ARGS="-r ${REMOTE_EXECUTION_ADDRESS}"
LOCAL_ARGS=""
if [ -n "${COMPATIBLE:-}" ]
then
    REMOTE_EXECUTION_ARGS="${REMOTE_EXECUTION_ARGS} --compatible"
    LOCAL_ARGS="--compatible"
fi

# To test the dispatch, we have a remote-execution server
# with a special variant of cat that
# - prefixes with some extra string, but
# - only takes the first argument into account.
# In this way, we can distinguish it from the regular cat on the
# on the normal remote execution server.
readonly REFERENCE_OUTPUT="FooOOOooo"
readonly SERVER_BIN_DIR="${TMPDIR}/server/bin"
mkdir -p "${SERVER_BIN_DIR}"
cat > "${SERVER_BIN_DIR}/cat" <<EOF
#!/bin/sh
echo "${REFERENCE_OUTPUT}"
$(which cat) "\$1"
EOF
chmod 755 "${SERVER_BIN_DIR}/cat"
cat "${SERVER_BIN_DIR}/cat"
echo

LOCAL_LAUNCHER='["env", "PATH='"${SERVER_BIN_DIR}:${PATH}"'"]'
echo "will use ${LOCAL_LAUNCHER} as local launcher"
echo

${JUST} execute --info-file "$INFOFILE" --pid-file "$PIDFILE" \
        --log-limit 6 --local-build-root ${ESDIR} ${LOCAL_ARGS} \
        -L "${LOCAL_LAUNCHER}" 2>&1 &

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

cat > dispatch.json <<EOF
[ [ {"server": "special"}, "127.0.0.1:${PORT}"] ]
EOF
cat dispatch.json
echo

touch ROOT

cat > TARGETS <<'EOF'
{ "payload":
  { "type": "generic"
  , "cmds": ["echo this-is-the-payload > payload.txt"]
  , "outs": ["payload.txt"]
  }
, "drop":
  { "type": "generic"
  , "cmds": ["echo please-drop-this > drop.txt"]
  , "outs": ["drop.txt"]
  }
, "post":
  { "type": "generic"
  , "cmds": ["echo this-is-added-at-the-end > post.txt"]
  , "outs": ["post.txt"]
  }
, "special-dispatch":
  { "type": "generic"
  , "cmds": ["cat payload.txt drop.txt > out.txt"]
  , "outs": ["out.txt"]
  , "execution properties":
    {"type": "singleton_map", "key": "server", "value": "special"}
  , "deps": ["payload", "drop"]
  }
, "":
  { "type": "generic"
  , "cmds": ["cat out.txt post.txt > final.txt"]
  , "deps": ["special-dispatch", "post"]
  , "outs": ["final.txt"]
  }
}
EOF

FAILED=""
"${JUST}" install -o . --local-build-root "${LBRDIR}" \
          ${REMOTE_EXECUTION_ARGS} --endpoint-configuration dispatch.json 2>&1 \
          || FAILED=YES
kill $(cat "${PIDFILE}")
[ -z "${FAILED}" ]
[ -f final.txt ]
echo
echo content of final.txt
cat final.txt
echo

grep ${REFERENCE_OUTPUT} final.txt
grep this-is-the-payload final.txt
grep drop final.txt && exit 1 || :

echo OK
