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

readonly REFERENCE_OUTPUT="FooOOoo"
readonly SERVER_BIN_DIR="${TMPDIR}/server/bin"
mkdir -p "${SERVER_BIN_DIR}"
cat > "${SERVER_BIN_DIR}/server-only-tool-foo" <<EOF
#!/bin/sh
echo -n "${REFERENCE_OUTPUT}"
EOF
chmod 755 "${SERVER_BIN_DIR}/server-only-tool-foo"
cat "${SERVER_BIN_DIR}/server-only-tool-foo"
echo

LOCAL_LAUNCHER='["env", "PATH='"${PATH}:${SERVER_BIN_DIR}"'"]'
echo "will use ${LOCAL_LAUNCHER} as local launcher"
echo

${JUST} execute --info-file "$INFOFILE" --pid-file "$PIDFILE" \
        --log-limit 6 --local-build-root ${ESDIR} \
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

touch ROOT

cat <<EOF > TARGETS
{ "":
  { "type": "generic"
  , "cmds": ["server-only-tool-foo > foo.txt"]
  , "outs": ["foo.txt"]
  }
}
EOF

"${JUST}" install -r localhost:${PORT} --local-build-root="${LBRDIR}" --local-launcher '["env", "--", "PATH=/nonexistent"]' -o . 2>&1
kill $(cat "${PIDFILE}")

readonly OUT=$(cat foo.txt)
echo "Found output: ${OUT}"

if ! [ "${OUT}" = "${REFERENCE_OUTPUT}" ]
then
  printf 'expecting "%s", got "%s"\n' "${REF}" "${OUT}" > /dev/stderr && exit 1
fi
