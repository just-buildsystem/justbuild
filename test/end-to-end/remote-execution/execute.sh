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
readonly LBR="${TEST_TMPDIR}/local-build-root"

REMOTE_EXECUTION_ARGS="-r ${REMOTE_EXECUTION_ADDRESS}"
LOCAL_ARGS=""
if [ -n "${COMPATIBLE:-}" ]
then
    REMOTE_EXECUTION_ARGS="${REMOTE_EXECUTION_ARGS} --compatible"
    LOCAL_ARGS="--compatible"
fi

mkdir work
cd work
touch ROOT

for i in `seq 1 32`
do
    cat > generate-$i.sh <<EOF
#!/bin/sh
  echo \$i > \$1
EOF
    chmod 755 generate-$i.sh
done

cat > TARGETS <<EOF
{ "":
  { "type": "install"
  , "dirs":
[ ["1", "1"]
EOF
for i in `seq 2 32`
do
    cat >> TARGETS <<EOF
, ["$i", "$i"]
EOF
done
echo ']}' >> TARGETS
for i in `seq 1 32`
do
    cat >> TARGETS <<EOF
, "$i": { "type": "generic"
        , "cmds": ["./generate-$i.sh $i.out"]
        , "outs": ["$i.out"]
        , "deps": ["generate-$i.sh"]
        }
EOF
done
echo '}' >> TARGETS

cat TARGETS
echo

# remote build should succeed
"${JUST}" build --local-build-root "${LBR}" \
          ${REMOTE_EXECUTION_ARGS} --dump-artifacts remote.json 2>&1
echo

# local build should succeed, and yield the same result
"${JUST}" build --local-build-root "${LBR}" \
          ${LOCAL_ARGS} --dump-artifacts local.json 2>&1

echo
diff remote.json local.json

echo OK
