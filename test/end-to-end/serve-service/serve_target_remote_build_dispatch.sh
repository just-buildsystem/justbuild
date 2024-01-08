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
# This script aims to test the correct dispatch of a build executed by the
# just serve endpoint.
#
###########################################################################

set -eu

env
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"

readonly LBRDIR="${TEST_TMPDIR}/local-build-root"
readonly ESDIR="${TEST_TMPDIR}/dispatch-build-root"
readonly OUTPUT="${TEST_TMPDIR}/output-dir"

readonly INFOFILE="${PWD}/info.json"
readonly PIDFILE="${PWD}/pid.txt"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
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

# set up the second just execute instance
${JUST} execute --info-file "${INFOFILE}" --pid-file "${PIDFILE}" \
        --log-limit 6 --local-build-root ${ESDIR} ${COMPAT} \
        -L "${LOCAL_LAUNCHER}" 2>&1 &

for _ in `seq 1 10`
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

FAILED=""

# set up the absent targets to build
mkdir work
cd work
touch ROOT

cat > dispatch.json <<EOF
[ [ {"server": "special"}, "127.0.0.1:${PORT}"] ]
EOF
cat dispatch.json
echo

cat > repos.json <<EOF
{ "main": "main"
, "repositories":
  { "main":
    { "repository":
      { "type": "git"
      , "commit": "$COMMIT_0"
      , "pragma": {"absent": true}
      , "repository": "http://non-existent.example.org/data.git"
      , "branch": "master"
      , "subdir": "."
      }
    }
  }
}
EOF

CONF=$("${JUST_MR}" --norc --local-build-root "${LBRDIR}" \
                    --remote-serve-address ${SERVE} \
                    -r ${REMOTE_EXECUTION_ADDRESS} ${COMPAT} \
                    setup)
cat $CONF
echo

# Check that we can build correctly
FAILED=""
${JUST} install --local-build-root "${LBRDIR}" -C "${CONF}" \
                --remote-serve-address ${SERVE} \
                -r "${REMOTE_EXECUTION_ADDRESS}" ${COMPAT} \
                --endpoint-configuration dispatch.json \
                --log-limit 4 \
                -o "${OUTPUT}" 2>&1 || FAILED=YES

kill $(cat "${PIDFILE}")
[ -z "${FAILED}" ]
[ -f "${OUTPUT}/final.txt" ]
echo
echo content of final.txt
cat "${OUTPUT}/final.txt"
echo

grep ${REFERENCE_OUTPUT} "${OUTPUT}/final.txt"
grep this-is-the-payload "${OUTPUT}/final.txt"
grep drop "${OUTPUT}/final.txt" && exit 1 || :

echo OK
