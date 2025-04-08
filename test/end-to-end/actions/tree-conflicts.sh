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

mkdir work
cd work
touch ROOT

cat > TARGETS <<'EOF'
{ "foo":
  { "type": "generic"
  , "out_dirs": ["out"]
  , "cmds": ["mkdir -p out", "echo FOO > out/foo", "echo FOOBAR > out/foobar"]
  }
, "good bar":
  { "type": "generic"
  , "out_dirs": ["out"]
  , "cmds": ["mkdir -p out", "echo BAR > out/bar", "echo FOOBAR > out/foobar"]
  }
, "bad bar":
  { "type": "generic"
  , "out_dirs": ["out"]
  , "cmds": ["mkdir -p out", "echo BAR > out/bar", "echo foobar > out/foobar"]
  }
, "spurious conflict":
  { "type": "disjoint_tree_overlay"
  , "name": "merge"
  , "deps": ["foo", "good bar"]
  }
, "real conflict":
  { "type": "disjoint_tree_overlay"
  , "name": "merge"
  , "deps": ["foo", "bad bar"]
  }
}
EOF

# In case of a spourious conflict, things should just build
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}/spurious" \
          -L '["env", "PATH='"${PATH}"'"]' 'spurious conflict' 2>&1
grep FOO "${OUT}/spurious/merge/out/foo"
grep BAR "${OUT}/spurious/merge/out/bar"
grep FOOBAR "${OUT}/spurious/merge/out/foobar"

# In case of a real conflict, analysis should still work
"${JUST}" analyse --local-build-root "${LBR}" \
          --dump-graph "${OUT}/graph.json" \
          --dump-artifacts-to-build "${OUT}/artifacts.json" \
          'real conflict' 2>&1
echo
[ "$(jq -r '.merge.type' "${OUT}/artifacts.json")" = TREE_OVERLAY ]
OVERLAY_ID=$(jq -r '.merge.data.id' "${OUT}/artifacts.json")
[ "$(jq '."tree_overlays"."'"${OVERLAY_ID}"'".trees | length' "${OUT}/graph.json")" -eq 2 ]

# Building should fail, however
"${JUST}" build --local-build-root "${LBR}" \
          -f "${OUT}/log" \
          -L '["env", "PATH='"${PATH}"'"]' 'real conflict' 2>&1 && exit 1 || :
echo
grep 'foobar' "${OUT}/log" # The location of the conflict should be mentioned

echo
echo OK
