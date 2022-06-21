#!/bin/sh

set -eu

export PATH=/bin:/usr/bin:$PATH

readonly OUTDIR=${TEST_TMPDIR}/out
readonly LBRDIR=${TEST_TMPDIR}/local-build-root

mkdir -p "${OUTDIR}"
mkdir -p "${LBRDIR}"

BOOTSTRAP_BIN=./bin/bootstrap.py

echo
echo Bootstrap
echo
python3 ${BOOTSTRAP_BIN} . "${OUTDIR}"/boot distdir
export JUST=$(realpath "${OUTDIR}"/boot/out/bin/just)

echo
echo Testing if we reached the fixed point
echo
readonly CONF=$(./bin/just-mr.py setup -C etc/repos.json --distdir=distdir --local-build-root="${LBRDIR}" just)
${JUST} install -C ${CONF} -o "${OUTDIR}"/final-out --local-build-root="${LBRDIR}"

sha256sum "${OUTDIR}"/boot/out/bin/just "${OUTDIR}"/final-out/bin/just
cmp "${OUTDIR}"/boot/out/bin/just "${OUTDIR}"/final-out/bin/just
