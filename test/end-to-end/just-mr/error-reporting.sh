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

set -eu

readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly BINDIR="${TEST_TMPDIR}/fake-bin"
mkdir -p "${BINDIR}"

mkdir work
cd work
touch ROOT

echo
echo '=== syntax error on CLI ==='
exit_code=0
echo '{"this is not valid json"}' > repos.json
"${JUST_MR}" --norc --local-build-root "${LBR}" --this-option-is-not-valid setup 2>&1 || exit_code=$?
echo "Exit code $exit_code"
[ "$exit_code" -eq 67 ]

echo
echo '=== CLI OK, but config not JSON ==='
exit_code=0
echo '{"this is not valid json"}' > repos.json
"${JUST_MR}" --norc --local-build-root "${LBR}" setup 2>&1 || exit_code=$?
echo "Exit code $exit_code"
[ "$exit_code" -eq 68 ] # config syntax error

echo
echo '=== everything syntactically OK, but: file repo, path missing ==='
exit_code=0
echo '{"repositories": {"": {"repository": {"type": "file"}}}}' > repos.json
"${JUST_MR}" --norc --local-build-root "${LBR}" setup 2>&1 || exit_code=$?
echo "Exit code $exit_code"
[ "$exit_code" -eq 71 ] # error during setup

echo
echo '=== everything syntactically OK, but: file repo, path not a string ==='
exit_code=0
echo '{"repositories": {"": {"repository": {"type": "file", "path": 4711}}}}' > repos.json
"${JUST_MR}" --norc --local-build-root "${LBR}" setup 2>&1 || exit_code=$?
echo "Exit code $exit_code"
[ "$exit_code" -eq 71 ] # error during setup

echo
echo '=== everything syntactically OK, but: file repo, path empty ==='
exit_code=0
echo '{"repositories": {"": {"repository": {"type": "file", "path": ""}}}}' > repos.json
"${JUST_MR}" --norc --local-build-root "${LBR}" setup 2>&1 || exit_code=$?
echo "Exit code $exit_code"
# The empty string is not a valid path, but it is OK to treat it as .
[ '(' "$exit_code" -eq 71 ')' -o '(' "$exit_code" -eq 0 ')' ]

echo
echo '=== setup sucessfull, but fork failed ==='
exit_code=0
echo '{"repositories": {"": {"repository": {"type": "file", "path": "."}}}}' > repos.json
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${BINDIR}/does-not-exist" build 2>&1 || exit_code=$?
echo "Exit code $exit_code"
[ "$exit_code" -eq 64 ]

echo
echo '=== non-zero exit code from build tool ==='
cat > "${BINDIR}/return-42" <<'EOI'
#!/bin/sh
exit 42
EOI
chmod 755 "${BINDIR}/return-42"
exit_code=0
echo '{"repositories": {"": {"repository": {"type": "file", "path": "."}}}}' > repos.json
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${BINDIR}/return-42" build 2>&1 || exit_code=$?
echo "Exit code $exit_code"
[ "$exit_code" -eq 42 ]

echo
echo '=== exit code 0 from build tool ==='
exit_code=0
echo '{"repositories": {"": {"repository": {"type": "file", "path": "."}}}}' > repos.json
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "true" build 2>&1 || exit_code=$?
echo "Exit code $exit_code"
[ "$exit_code" -eq 0 ]


echo
echo OK
