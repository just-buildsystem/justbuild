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
readonly WRKDIR="${PWD}"
readonly LBRDIR_A="${TEST_TMPDIR}/local-build-root/instance-A"
readonly LBRDIR_B="${TEST_TMPDIR}/local-build-root/instance-B"
readonly LBRDIR_C="${TEST_TMPDIR}/local-build-root/instance-C"
readonly SRCDIR_A="${TEST_TMPDIR}/src/instance-A"
readonly SRCDIR_B="${TEST_TMPDIR}/src/instance-B"
readonly SRCDIR_C="${TEST_TMPDIR}/src/instance-C"
readonly OUTDIR_B="${TEST_TMPDIR}/out/instance-B"
readonly OUTDIR_C="${TEST_TMPDIR}/out/instance-C"

REMOTE_EXECUTION_ARGS="-r ${REMOTE_EXECUTION_ADDRESS}"
REMOTE_EXECUTION_PROPS=""
LOCAL_ARGS=""
if [ "${REMOTE_EXECUTION_PROPERTIES:-}" != "" ]
then
   REMOTE_EXECUTION_PROPS="$(printf " --remote-execution-property %s" ${REMOTE_EXECUTION_PROPERTIES})"
fi
if [ -n "${COMPATIBLE:-}" ]
then
    REMOTE_EXECUTION_ARGS="${REMOTE_EXECUTION_ARGS} --compatible"
    LOCAL_ARGS="--compatible"
fi

# install-cas from remote execution endpoint

## instance A to upload
mkdir -p "${SRCDIR_A}"
cd "${SRCDIR_A}"
touch ROOT

mkdir -p tree/foo/bar
mkdir -p tree/baz
echo content frist file > tree/foo/bar/data.txt
echo content second file > tree/baz/data.txt
ln -s dummy tree/foo/link

cat > TARGETS <<'EOF'
{"": {"type": "install", "dirs": [[["TREE", null, "tree"], "."]]}}
EOF

"${JUST}" build --local-build-root "${LBRDIR_A}" \
          --dump-artifacts "${WRKDIR}/tree.json" \
          ${REMOTE_EXECUTION_ARGS} ${REMOTE_EXECUTION_PROPS} 2>&1

## instance B should be able to fetch

cd "${WRKDIR}"
TREE_ID=$(jq -r '.tree.id' "${WRKDIR}/tree.json")"::t"
"${JUST}" install-cas --local-build-root "${LBRDIR_B}" \
          -o "${OUTDIR_B}/first" ${REMOTE_EXECUTION_ARGS} "${TREE_ID}" 2>&1

for f in $(cd "${SRCDIR_A}/tree" && find . -type f)
do
    cmp "${SRCDIR_A}/tree/$f" "${OUTDIR_B}/first/$f"
done
for l in $(cd "${SRCDIR_A}/tree" && find . -type L)
do
    cmp $(readlink "${SRCDIR_A}/tree/$l") $(readlink "${OUTDIR_B}/first/$l")
done

## ... and to remember

"${JUST}" install-cas --local-build-root "${LBRDIR_B}" \
          -o "${OUTDIR_B}/first-discard" \
          --remember ${REMOTE_EXECUTION_ARGS} "${TREE_ID}" 2>&1

"${JUST}" install-cas --local-build-root "${LBRDIR_B}" \
          -o "${OUTDIR_B}/first-from-local" ${LOCAL_ARGS} "${TREE_ID}" 2>&1

for f in $(cd "${SRCDIR_A}/tree" && find . -type f)
do
    cmp "${SRCDIR_A}/tree/$f" "${OUTDIR_B}/first-from-local/$f"
done
for l in $(cd "${SRCDIR_A}/tree" && find . -type L)
do
    cmp $(readlink "${SRCDIR_A}/tree/$l") $(readlink "${OUTDIR_B}/first-from-local/$l")
done


# install-cas has to prefer (at least: use) local CAS, also with remote endpoint

## instance B builds locally, to fill local CAS
## (using a different tree, not known to the remote execution)

mkdir -p "${SRCDIR_B}"
cd "${SRCDIR_B}"
touch ROOT

mkdir -p tree/different/foobar
mkdir -p tree/bar/other_dir
mkdir -p tree/another_dir
echo some other content > tree/different/foobar/file.txt
echo yet another content > tree/bar/other_dir/file.txt
ln -s dummy tree/another_dir/link

cat > TARGETS <<'EOF'
{"": {"type": "install", "dirs": [[["TREE", null, "tree"], "."]]}}
EOF

"${JUST}" build --local-build-root "${LBRDIR_B}" \
          --dump-artifacts "${WRKDIR}/tree.json" ${LOCAL_ARGS} 2>&1

## install-cas should work, even though the tree is not known remotely

cd "${WRKDIR}"
TREE_ID=$(jq -r '.tree.id' "${WRKDIR}/tree.json")"::t"
"${JUST}" install-cas --local-build-root "${LBRDIR_B}" \
          -o "${OUTDIR_B}/second" ${REMOTE_EXECUTION_ARGS} "${TREE_ID}" 2>&1

for f in $(cd "${SRCDIR_B}/tree" && find . -type f)
do
    cmp "${SRCDIR_B}/tree/$f" "${OUTDIR_B}/second/$f"
done
for l in $(cd "${SRCDIR_B}/tree" && find . -type L)
do
    cmp $(readlink "${SRCDIR_B}/tree/$l") $(readlink "${OUTDIR_B}/second/$l")
done

# install --remember

## install a tree with --remember, this also tests blob splitting for:
##  - file smaller than minimum chunk size (2 KB)
##  - file larger than maximum chunk size (64 KB)
##  - tree smaller than minimum chunk size
##  - tree larger than maximum chunk size

mkdir -p "${SRCDIR_C}"
cd "${SRCDIR_C}"
touch ROOT

cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "out_dirs": ["out"]
  , "cmds":
    [ "mkdir -p out/foo out/bar out/baz out/large"
    , "echo some file content > out/foo/data.txt"
    , "echo more file content > out/bar/file.txt"
    , "echo even more file content > out/bar/another_file.txt"
    , "ln -s dummy out/baz/link"
    , "for i in $(seq 1000); do echo foo > out/large/$(printf %064d $i).txt; done"
    , "for i in $(seq 128); do seq 1 1024 >> out/large.txt; done"
    ]
  }
}
EOF

"${JUST}" install --local-build-root "${LBRDIR_C}" \
          --remember -o "${OUTDIR_C}/remote" \
          --dump-artifacts "${WRKDIR}/tree.json" \
          ${REMOTE_EXECUTION_ARGS} ${REMOTE_EXECUTION_PROPS} 2>&1

## now it should also be available for local installation

TREE_ID=$(jq -r ".\"${OUTDIR_C}/remote/out\".id" "${WRKDIR}/tree.json")"::t"
"${JUST}" install-cas --local-build-root "${LBRDIR_C}" \
          -o "${OUTDIR_C}/local" \
          ${LOCAL_ARGS} "${TREE_ID}" 2>&1

for f in $(cd "${OUTDIR_C}/remote/out" && find . -type f)
do
    cmp "${OUTDIR_C}/remote/out/$f" "${OUTDIR_C}/local/$f"
done
for l in $(cd "${OUTDIR_C}/remote/out" && find . -type L)
do
    cmp $(readlink "${OUTDIR_C}/remote/out/$l") $(readlink "${OUTDIR_C}/local/$l")
done

echo OK
