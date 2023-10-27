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

readonly JUST="$PWD/bin/tool-under-test"
readonly JUST_MR="$PWD/bin/mr-tool-under-test"
readonly LBRDIR="$TEST_TMPDIR/local-build-root"
readonly OUT="$TEST_TMPDIR/out"

touch ROOT
cat > repos.json <<'EOF'
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "foo", "pragma": {"to_git": true}}
    , "bindings": {"bar": "bar"}
    }
  , "bar":
    {"repository": {"type": "file", "path": "bar", "pragma": {"to_git": true}}}
  }
}
EOF

mkdir bar
cat > bar/TARGETS <<'EOF'
{"it": {"type": "file_gen", "name": "it", "data": "target it in bar"}}
EOF

mkdir foo
echo -n 'file it in foo' > foo/it
cat > foo/TARGETS <<'EOF'
{ "it": {"type": "file_gen", "name": "it", "data": "target it in foo"}
, "local": {"type": "export", "target": ["", "it"]}
, "distant": {"type": "export", "target": ["@", "bar", "", "it"]}
, "file": {"type": "export", "target": ["FILE", null, "it"]}
}
EOF

CONF=$("${JUST_MR}" --local-build-root "${LBRDIR}" setup '')

"${JUST}" install --local-build-root "${LBRDIR}" -C "${CONF}" -o "${OUT}" local 2>&1
cat ${OUT}/it
echo
grep 'target.*foo' ${OUT}/it
echo

"${JUST}" install --local-build-root "${LBRDIR}" -C "${CONF}" -o "${OUT}" distant 2>&1
cat ${OUT}/it
echo
grep 'target.*bar' ${OUT}/it
echo

"${JUST}" install --local-build-root "${LBRDIR}" -C "${CONF}" -o "${OUT}" file 2>&1
cat ${OUT}/it
echo
grep 'file.*foo' ${OUT}/it
echo
