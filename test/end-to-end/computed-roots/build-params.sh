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
readonly LBR="$TMPDIR/local-build-root"
readonly JUST="${ROOT}/bin/tool-under-test"
readonly OUT="${ROOT}/out"

readonly BASE_ROOT="${ROOT}/base"
mkdir -p "${BASE_ROOT}"
cd "${BASE_ROOT}"
cat > TARGETS <<'EOF'
{ "": {"type": "export", "target": "root"}
, "root":
  { "type": "install"
  , "files": {"TARGETS": "payload-target", "out": "unrelated"}
  }
}
EOF
cat > payload-target <<'EOF'
{"": {"type": "generic", "outs": ["out"], "cmds": ["echo GoodOutput > out"]}}
EOF
echo DoNOTLookAtMe > unrelated
git init 2>&1
git branch -m stable-1.0 2>&1
git config user.email "nobody@example.org" 2>&1
git config user.name "Nobody" 2>&1
git add . 2>&1
git commit -m "Initial commit" 2>&1
GIT_TREE=$(git log -n 1 --format="%T")

mkdir -p "${ROOT}/main"
cd "${ROOT}/main"

cat > repo-config.json <<EOF
{ "repositories":
  { "base":
    {"workspace_root": ["git tree", "${GIT_TREE}", "${BASE_ROOT}/.git"]}
  , "": {"workspace_root": ["computed", "base", "", "", {}]}
  }
}
EOF
cat repo-config.json


echo
echo Building with option -P
echo
"${JUST}" build --local-build-root "${LBR}" -C repo-config.json \
          -P out > "${OUT}/stdout" 2> "${OUT}/stderr"

grep GoodOutput "${OUT}/stdout"
grep DoNOT "${OUT}/stdout" && exit 1 || :

echo OK
