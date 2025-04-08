#!/bin/sh
# Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

readonly ROOT="$(pwd)"
readonly LBR="${TMPDIR}/local-build-root"
readonly OUT="${ROOT}/out"
mkdir -p "${OUT}"
readonly JUST="${ROOT}/bin/tool-under-test"
readonly JUST_MR="${ROOT}/bin/mr-tool-under-test"
readonly ETC_DIR="${ROOT}/etc"
readonly WRKDIR="${ROOT}/work"


mkdir -p "${ETC_DIR}"
readonly RC="${ETC_DIR}/rc.json"
cat > "${RC}" <<EOF
{ "just": {"root": "system", "path": "${JUST#/}"}
, "local build root": {"root": "system", "path": "${LBR#/}"}
, "remote execution":
  { "address": "${REMOTE_EXECUTION_ADDRESS}"
EOF
if [ -n "${COMPATIBLE:-}" ]
then
cat >> "${RC}" <<EOF
  , "compatible": true
EOF
fi
cat >> "${RC}" <<EOF
  }
}
EOF
cat "${RC}"

mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": "."}}}}
EOF

cat > TARGETS <<'EOF'
{ "early":
  { "type": "generic"
  , "out_dirs": ["out"]
  , "cmds":
    [ "mkdir -p out/foo out/mixed"
    , "for i in `seq 1 20`; do echo FOO > out/foo/data$i.txt; done"
    , "for i in `seq 1 19`; do echo early > out/mixed/data$i.txt; done"
    ]
  }
, "late":
  { "type": "generic"
  , "out_dirs": ["out"]
  , "cmds":
    [ "mkdir -p out/bar out/mixed"
    , "for i in `seq 1 20`; do echo BAR > out/bar/data$i.txt; done"
    , "for i in `seq 10 30`; do echo late > out/mixed/data$i.txt; done"
    ]
  }
, "overlay":
  {"type": "tree_overlay", "name": "overlay", "deps": ["early", "late"]}
, "TheOffendingTarget":
  { "type": "disjoint_tree_overlay"
  , "name": "overlay"
  , "deps": ["early", "late"]
  }
, "conflict": {"type": "install", "dirs": [["TheOffendingTarget", ""]]}
}
EOF


# Conflict-free build
"${JUST_MR}" --rc "${RC}" install -o "${OUT}/overlay" overlay 2>&1
grep FOO "${OUT}/overlay/overlay/out/foo/data15.txt"
grep BAR "${OUT}/overlay/overlay/out/bar/data15.txt"
grep early "${OUT}/overlay/overlay/out/mixed/data5.txt"
grep late "${OUT}/overlay/overlay/out/mixed/data15.txt"
grep late "${OUT}/overlay/overlay/out/mixed/data25.txt"


# Analysis of conflict should work
"${JUST_MR}" --rc "${RC}" analyse \
             --dump-graph "${OUT}/graph.json" \
             --dump-artifacts-to-build "${OUT}/artifacts.json" \
             conflict 2>&1

echo
[ "$(jq -r '.overlay.type' "${OUT}/artifacts.json")" = TREE_OVERLAY ]
OVERLAY_ID=$(jq -r '.overlay.data.id' "${OUT}/artifacts.json")
[ "$(jq '."tree_overlays"."'"${OVERLAY_ID}"'".trees | length' "${OUT}/graph.json")" -eq 2 ]

# Building the tree conflict should fail with a reasonable error message
"${JUST_MR}" --rc "${RC}" build \
             -f "${OUT}/log" conflict 2>&1 && exit 1 || :
grep 'mixed/data1..txt' "${OUT}/log"
grep 'TheOffendingTarget' "${OUT}/log"

echo OK
