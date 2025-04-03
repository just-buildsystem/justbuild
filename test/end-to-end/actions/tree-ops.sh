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

set -e

readonly ROOT="$(pwd)"
readonly LBR="${TMPDIR}/local-build-root"
readonly OUT="${ROOT}/out"
mkdir -p "${OUT}"
readonly JUST="${ROOT}/bin/tool-under-test"

mkdir work
cd work
touch ROOT

cat > TARGETS <<'EOF'
{ "early":
  { "type": "generic"
  , "out_dirs": ["out"]
  , "cmds": ["mkdir -p out", "echo FOO > out/foo", "echo early > out/conflict"]
  }
, "late":
  { "type": "generic"
  , "out_dirs": ["out"]
  , "cmds": ["mkdir -p out", "echo BAR > out/bar", "echo late > out/conflict"]
  }
, "overlay":
  {"type": "tree_overlay", "name": "overlay", "deps": ["early", "late"]}
}
EOF

# Sanity check: the trees to be overlayed are build correctly
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/early" \
          -L '["env", "PATH='"${PATH}"'"]' early 2>&1
grep FOO "${OUT}/early/out/foo"
grep early "${OUT}/early/out/conflict"
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/late" \
          -L '["env", "PATH='"${PATH}"'"]' late 2>&1
grep BAR "${OUT}/late/out/bar"
grep late "${OUT}/late/out/conflict"

# Analysis of the overlay target should show an overlay action of two trees
"${JUST}" analyse --local-build-root "${LBR}" \
          --dump-graph "${OUT}/graph.json" \
          --dump-artifacts-to-build "${OUT}/artifacts.json" \
          overlay 2>&1
echo
cat "${OUT}/graph.json"
echo
[ "$(jq -r '.overlay.type' "${OUT}/artifacts.json")" = TREE_OVERLAY ]
OVERLAY_ID=$(jq -r '.overlay.data.id' "${OUT}/artifacts.json")
[ "$(jq '."tree_overlays"."'"${OVERLAY_ID}"'".trees | length' "${OUT}/graph.json")" -eq 2 ]

# Actual test: the overlay is built correctly
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/overlay" \
          --log-limit 5 \
          -L '["env", "PATH='"${PATH}"'"]' overlay 2>&1
grep FOO "${OUT}/overlay/overlay/out/foo"
grep BAR "${OUT}/overlay/overlay/out/bar"
grep late "${OUT}/overlay/overlay/out/conflict"

echo OK
