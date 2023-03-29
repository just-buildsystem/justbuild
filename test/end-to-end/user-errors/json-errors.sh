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

set -e

readonly ROOT="${PWD}"
readonly JUST="${ROOT}/bin/tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"

touch ROOT
cat > TARGETS <<'EOF'
{ "THIS": would be the default target if it where syntactically correct ...
EOF

"${JUST}" build --local-build-root "${LBR}" 2>msg && exit 1 || :
cat msg
grep -q 'default target' msg
grep -q 'TARGETS' msg
grep -q 'line' msg # expect  to see a position of the parsing error
grep -q '"THIS"' msg # expect to see the last valid token


"${JUST}" build --local-build-root "${LBR}" some-target 2>msg && exit 1 || :
cat msg
# expect the error to happen while trying to analyse the default target
grep -q '["@","","","some-target"]' msg
grep -q 'line' msg # expect  to see a position of the parsing error
grep -q '"THIS"' msg # expect to see the last valid token

echo OK
