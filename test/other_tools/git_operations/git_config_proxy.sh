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
# Run Git config proxy settings retrieval tests for various scenarios
#
# Expected Git config (relative) file path: gitconfig
##

readonly ROOT=`pwd`
readonly TESTEXEC="${ROOT}/src/git_config_run_test"

# ensure clean env for this type of test
unset all_proxy
unset ALL_PROXY
unset no_proxy
unset NO_PROXY
unset http_proxy
unset https_proxy
unset HTTPS_PROXY

# Set up the test scenarios to be run in parallel
k=1  # scenarios counter

echo
echo "Scenario $k: default (clean env and missing config)"
k=$((k+1))
envcmd=""
url="https://192.0.2.1"
result=""
$(
cd $(mktemp -d)
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: proxy from all_proxy envariable"
k=$((k+1))
envcmd="all_proxy=198.51.100.1:50000"
url="https://example.com"  # scheme must match envariable
result="http://198.51.100.1:50000/"  # proxy default scheme is http
$(
cd $(mktemp -d)
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: proxy from ALL_PROXY envariable"
k=$((k+1))
envcmd="ALL_PROXY=198.51.100.1:50000"
url="https://example.com"  # scheme must match envariable
result="http://198.51.100.1:50000/"  # proxy default scheme is http
$(
cd $(mktemp -d)
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: lowercase all_proxy envariable priority"
k=$((k+1))
envcmd="all_proxy=198.51.100.1:50000 ALL_PROXY=192.0.2.1:50505"
url="https://example.com"  # scheme does not match envariable
result="http://198.51.100.1:50000/"  # proxy default scheme is http
$(
cd $(mktemp -d)
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: proxy from http_proxy"
k=$((k+1))
envcmd="http_proxy=198.51.100.1:50000"
url="http://example.com"  # scheme must match envariable
result="http://198.51.100.1:50000/"  # proxy default scheme is http
$(
cd $(mktemp -d)
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: proxy from https_proxy"
k=$((k+1))
envcmd="https_proxy=198.51.100.1:50000"
url="https://example.com"  # scheme must match envariable
result="http://198.51.100.1:50000/"  # proxy default scheme is http
$(
cd $(mktemp -d)
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: proxy from HTTPS_PROXY"
k=$((k+1))
envcmd="HTTPS_PROXY=198.51.100.1:50000"
url="https://example.com"  # scheme must match envariable
result="http://198.51.100.1:50000/"  # proxy default scheme is http
$(
cd $(mktemp -d)
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: scheme mismatch in envariable"
k=$((k+1))
envcmd="https_proxy=198.51.100.1:50000"
url="http://example.com"  # scheme does not match envariable
result=""
$(
cd $(mktemp -d)
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: lowercase http_proxy envariable priority"
k=$((k+1))
envcmd="https_proxy=198.51.100.1:50000 HTTPS_PROXY=192.0.2.1:50505"
url="https://example.com"  # scheme does not match envariable
result="http://198.51.100.1:50000/"  # proxy default scheme is http
$(
cd $(mktemp -d)
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: specific and generic envariables priority"
k=$((k+1))
envcmd="http_proxy=198.51.100.1:50000 all_proxy=192.0.2.1:50505"
url="http://example.com"  # scheme must match envariable
result="http://198.51.100.1:50000/"  # proxy default scheme is http
$(
cd $(mktemp -d)
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: proxy via http.proxy config entry"
k=$((k+1))
envcmd=""
url="https://example.com"
result="http://198.51.100.1:50000/"  # proxy default scheme is http
$(
cd $(mktemp -d)
cat > gitconfig <<INNER
[http]
    proxy = 198.51.100.1:50000
INNER
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: proxy via http.<url>.proxy config entry"
k=$((k+1))
envcmd=""
url="https://example.com"
result="http://198.51.100.1:50000/"  # proxy default scheme is http
$(
cd $(mktemp -d)
cat > gitconfig <<INNER
[http "https://example.com"]
    proxy = 198.51.100.1:50000
INNER
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: config entry specificity priority"
k=$((k+1))
envcmd=""
url="https://example.com"
result="http://198.51.100.1:50000/"  # proxy default scheme is http
$(
cd $(mktemp -d)
cat > gitconfig <<INNER
[http]
    proxy = 192.0.2.1:50505
[http "https://example.com"]
    proxy = 198.51.100.1:50000
INNER
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: config entry last best match wins"
k=$((k+1))
envcmd=""
url="https://example.com.org" # url needs two subdomains with same length
result="http://198.51.100.1:50000/"  # proxy default scheme is http
$(
cd $(mktemp -d)
cat > gitconfig <<INNER
[http "https://example.*.org"]
    proxy = 192.0.2.1:50505
[http "https://example.com.*"]
    proxy = 198.51.100.1:50000
INNER
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: disable proxy via http.<url>.proxy config entry"
k=$((k+1))
envcmd=""
url="https://example.com"
result=""  # no proxy
$(
cd $(mktemp -d)
cat > gitconfig <<INNER
[http]
    proxy = 192.0.2.1:50505
[http "https://example.com"]
    proxy =
INNER
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: ignored empty http.proxy config entry"
k=$((k+1))
envcmd=""
url="https://example.com"
result="http://198.51.100.1:50000/"  # no proxy
$(
cd $(mktemp -d)
cat > gitconfig <<INNER
[http]
    proxy =
[http "https://example.com"]
    proxy = 198.51.100.1:50000
INNER
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: disable proxy with NO_PROXY envariable"
k=$((k+1))
envcmd="NO_PROXY=foo,.com"
url="https://example.com"
result=""  # no proxy
$(
cd $(mktemp -d)
cat > gitconfig <<INNER
[http]
    proxy = 198.51.100.1:50000
INNER
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: disable proxy with no_proxy envariable"
k=$((k+1))
envcmd="no_proxy=foo,.example.com"
url="http://example.com"
result=""  # no proxy
$(
cd $(mktemp -d)
cat > gitconfig <<INNER
[http]
    proxy = 198.51.100.1:50000
INNER
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"

echo
echo "Scenario $k: unmatched no_proxy envariables fallthrough"
k=$((k+1))
envcmd="no_proxy=.com:50505 NO_PROXY=*.com"  # port must match; wildcard misuse
url="http://example.com"
result="http://198.51.100.1:50000/"
$(
cd $(mktemp -d)
cat > gitconfig <<INNER
[http]
    proxy = 198.51.100.1:50000
INNER
eval "${envcmd} ${TESTEXEC} proxy ${url} ${result}"
cd ${ROOT}
)
echo "PASSED"
