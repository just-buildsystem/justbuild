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
# By design, when a just-serve instance is created, if no remote-execution
# endpoint is provided, the same process will act also as just-execute.
#
###########################################################################

set -eu
env
readonly JUST="${PWD}/bin/tool-under-test"

readonly LBR="${TEST_TMPDIR}/local-build-root"

readonly INFOFILE="${PWD}/info.json"
readonly PIDFILE="${PWD}/pid.txt"

# test that, if no remote endpoint is passed to just-serve, it will spawn a
# just-execute instance
COMPAT=""
cat > .just-servec <<EOF
{ "repositories": []
, "remote service": {"info file": "${INFOFILE}", "pid file": "${PIDFILE}"}
, "local build root": "${LBR}"
EOF
if [ "${COMPATIBLE:-}" = "YES" ]
then
  COMPAT="--compatible"
  cat >> .just-servec <<EOF
, "execution endpoint": {"compatible": true}
EOF
fi
cat >> .just-servec <<EOF
}
EOF
echo "Serve service configuration:"
cat .just-servec

"${JUST}" serve .just-servec 2>&1 &

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

cleanup() {
  kill $(cat "${PIDFILE}")
}
trap cleanup EXIT

touch ROOT
cat > TARGETS <<ENDTARGETS
{ "":
  { "type": "generic"
  , "cmds": ["echo hello from just-serve-just-execute > out.txt"]
  , "outs": ["out.txt"]
  }
}

ENDTARGETS

"${JUST}" install --local-build-root "${LBR}" \
                  -r localhost:${PORT} ${COMPAT} -o .
grep 'just-serve-just-execute' out.txt

# test that if we only pass --remote-serve-address it is also used as remote
# execution endpoint

rm -rf "${LBR}"
"${JUST}" install --local-build-root "${LBR}" \
                  --remote-serve-address localhost:${PORT} ${COMPAT} -o .
grep 'just-serve-just-execute' out.txt
