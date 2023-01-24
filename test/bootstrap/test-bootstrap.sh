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

export PATH=/bin:/usr/bin:$PATH

readonly OUTDIR=${TEST_TMPDIR}/out
readonly LBRDIR=${TEST_TMPDIR}/local-build-root
readonly EMPTY=${TEST_TMPDIR}/empty
readonly PRUNED_CONFIG=${TEST_TMPDIR}/pruned_config

mkdir -p "${OUTDIR}"
mkdir -p "${LBRDIR}"
mkdir -p "${EMPTY}"

BOOTSTRAP_BIN=./bin/bootstrap.py

echo
echo Bootstrap
echo
python3 ${BOOTSTRAP_BIN} . "${OUTDIR}"/boot distdir
export JUST=$(realpath "${OUTDIR}"/boot/out/bin/just)

echo
echo Testing if we reached the fixed point
echo
./prune-config.py etc/repos.json ${PRUNED_CONFIG} ${EMPTY}
cat ${PRUNED_CONFIG}
echo
readonly CONF=$(./bin/just-mr.py -C ${PRUNED_CONFIG} --distdir=distdir --local-build-root="${LBRDIR}" setup just)
${JUST} install -C ${CONF} -o "${OUTDIR}"/final-out --local-build-root="${LBRDIR}"

sha256sum "${OUTDIR}"/boot/out/bin/just "${OUTDIR}"/final-out/bin/just
cmp "${OUTDIR}"/boot/out/bin/just "${OUTDIR}"/final-out/bin/just
