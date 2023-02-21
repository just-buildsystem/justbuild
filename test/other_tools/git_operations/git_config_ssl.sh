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

###
# Run Git config SSL settings retrieval tests for various scenarios
#
# Expected Git config (relative) file path: gitconfig
#
#
##

readonly ROOT=`pwd`
readonly TESTEXEC="${ROOT}/src/git_config_run_test"

# ensure clean env for this type of test
unset GIT_SSL_NO_VERIFY

# run the test scenarios
k=1  # scenarios counter

echo
echo "Scenario $k: missing gitconfig"
k=$((k+1))
$(
cd $(mktemp -d)
${TESTEXEC} SSL https://192.0.2.1 1
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: empty gitconfig"
k=$((k+1))
$(
cd $(mktemp -d)
touch gitconfig
${TESTEXEC} SSL https://example.com 1
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: disable SSL check via http.sslVerify"
k=$((k+1))
$(
cd $(mktemp -d)
cat > gitconfig <<INNER
[http]
    sslVerify = false
INNER
${TESTEXEC} SSL https://example.com
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: disable SSL check via http.<url>.sslVerify"
k=$((k+1))
$(
cd $(mktemp -d)
cat > gitconfig <<INNER
[http "https://example.com"]
    sslVerify = false
INNER
${TESTEXEC} SSL https://example.com
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: enable SSL check via http.sslVerify, but disable via envariable"
k=$((k+1))
$(
cd $(mktemp -d)
cat > gitconfig <<INNER
[http]
    sslVerify = true
INNER
GIT_SSL_NO_VERIFY= ${TESTEXEC} SSL https://example.com
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: check priority of gitconfig entries"
k=$((k+1))
$(
cd $(mktemp -d)
cat > gitconfig <<INNER
[http]
    sslVerify = true
[http "https://example.com"]
    sslVerify = false
INNER
${TESTEXEC} SSL https://example.com
cd ${ROOT}
)
echo "PASSED"
