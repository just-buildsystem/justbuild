#!/bin/sh

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

env LOCALBASE=${LOCALBASE} PACKAGE=YES python3 ${WRKSRC}/bin/bootstrap.py ${WRKSRC} ${WRKDIR}

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
