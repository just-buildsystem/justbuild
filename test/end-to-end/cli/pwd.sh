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

ROOT=$(pwd)
TOOL=$(realpath ./bin/tool-under-test)
mkdir -p .root
BUILDROOT=$(realpath .root)
mkdir -p out
OUTDIR=$(realpath out)


mkdir src
touch src/ROOT
cat > src/TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["hello.txt"]
  , "cmds": ["echo Hello World > hello.txt"]
  }
}
EOF
SRCDIR=$(realpath src)

cat > repos.json <<EOF
{"repositories": {"": {"workspace_root": ["file", "${SRCDIR}"]}}}
EOF
REPOS=$(realpath repos.json)
cat "${REPOS}"

# Verify that building on a sufficiently absolute configuraiton
# does not require a current working directory

ENTERED_DIR_FILE="${ROOT}/entered"
WORK_DIR="${ROOT}/working-directory"
WORK_DIR_REMOVED_FILE="${ROOT}/removed"
DONE_FILE="${ROOT}/build_attempt_done"
WORKER_LOG="${ROOT}/log"

cat > worker.sh <<EOF
#!/bin/sh
cd "${WORK_DIR}"
touch "${ENTERED_DIR_FILE}"
while [ ! -f "${WORK_DIR_REMOVED_FILE}" ]
do
  sleep 1
done
${TOOL} install --local-build-root "${ROOT}" -C "${REPOS}" -o "${OUTDIR}" > "${WORKER_LOG}" 2>&1
touch "${DONE_FILE}"
EOF
chmod 755 worker.sh

echo
cat worker.sh
echo

mkdir -p "${WORK_DIR}"
echo Created "${WORK_DIR}", starting worker
(./worker.sh) &
echo Wating for the worker to enter its work dir
while [ ! -f "${ENTERED_DIR_FILE}" ]
do
    sleep 1
done
echo Removing work dir
rm -rf "${WORK_DIR}"
touch "${WORK_DIR_REMOVED_FILE}"
echo Waiting for the worker to finish
while [ ! -f "${DONE_FILE}" ]
do
    sleep 1
done
echo Worker finished, output was
cat "${WORKER_LOG}"
echo

echo Inspecting result
ls "${OUTDIR}"
grep World "${OUTDIR}/hello.txt"

echo OK
