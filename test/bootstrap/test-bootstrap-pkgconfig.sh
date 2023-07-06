#!/bin/sh
# Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

# Set paths

export PATH=/bin:/usr/bin:$PATH

readonly LOCALBASE=`pwd`/LOCALBASE
readonly WRKSRC=`pwd`/srcs/just
readonly WRKDIR=${TMPDIR}/work-just-bootstrap
mkdir -p ${WRKDIR}
readonly TESTDIR=${TMPDIR}/work-test
mkdir -p ${TESTDIR}
readonly TEST_BUILD_ROOT=${TMPDIR}/.just
mkdir -p ${TEST_BUILD_ROOT}
readonly TEST_OUT_DIR=${TMPDIR}/work-test-out
mkdir -p ${TEST_OUT_DIR}

# bootstrap command

env LOCALBASE=${LOCALBASE} \
    PACKAGE=YES \
    JUST_BUILD_CONF='{"TOOLCHAIN_CONFIG":{"FAMILY": "clang"}, "PKG_CONFIG_ARGS":["--define-prefix"]}' \
    python3 ${WRKSRC}/bin/bootstrap.py ${WRKSRC} ${WRKDIR} 2>&1

# Do some sanity checks with the binary

JUST=${WRKDIR}/out/bin/just
echo Bootstrap finished. Obtained ${JUST}

echo
${JUST} -h
echo
${JUST} version
echo
touch ${TESTDIR}/ROOT
cat > ${TESTDIR}/TARGETS <<'EOF'
{ "hello world":
  { "type": "generic"
  , "cmds": ["echo Hello World > out.txt"]
  , "outs": ["out.txt"]
  }
, "":
  { "type": "generic"
  , "cmds": ["cat out.txt | tr a-z A-Z > final.txt"]
  , "outs": ["final.txt"]
  , "deps": ["hello world"]
  }
}
EOF

echo ${JUST} install -o ${TEST_OUT_DIR} --workspace-root ${TESTDIR} --local-build-root ${TEST_BUILD_ROOT} '' ''
${JUST} install -o ${TEST_OUT_DIR} --workspace-root ${TESTDIR} --local-build-root ${TEST_BUILD_ROOT} '' ''

grep HELLO ${TEST_OUT_DIR}/final.txt

echo OK
