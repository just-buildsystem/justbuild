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

set -e

TOOL=$(realpath ./bin/tool-under-test)
mkdir -p .root
BUILDROOT=$(realpath .root)
mkdir -p out
OUTDIR=$(realpath out)


mkdir src
cd src
touch ROOT

cat > repos.json <<'EOF'
{"repositories": {"": {"target_file_name": "TARGETS.local"}}}
EOF
export CONF=$(realpath repos.json)

cat > TARGETS.local <<'EOF'
{"a": {"type": "file_gen", "name": "a.txt", "data": "A-top-level"}}
EOF

echo
echo === Default target ===
${TOOL} build -C $CONF --local-build-root ${BUILDROOT} -Pa.txt | grep top-level


echo
echo === top-level module found ===

mkdir foo
cd foo
cat > TARGETS <<'EOF'
{"a": {"type": "file_gen", "name": "a.txt", "data": "WRONG"}}
EOF
${TOOL} build -C $CONF --local-build-root ${BUILDROOT} -Pa.txt | grep top-level

echo === correct root referece ===

cat > TARGETS.local <<'EOF'
{"a": {"type": "file_gen", "name": "b.txt", "data": "A-local"}
, "": {"type": "install", "deps": ["a", ["", "a"]]}
}
EOF

${TOOL} install -C $CONF --local-build-root ${BUILDROOT} -o ${OUTDIR}/top-ref 2>&1
echo
grep top-level ${OUTDIR}/top-ref/a.txt
grep local ${OUTDIR}/top-ref/b.txt

echo
echo === DONE ===
