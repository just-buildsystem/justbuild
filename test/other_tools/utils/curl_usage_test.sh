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

# cleanup of http.server; pass server_pid as arg
server_cleanup() {
  echo "Shut down HTTP server"
  # send SIGTERM
  kill ${1} & res=$!
  wait ${res}
  echo "done"
}

readonly ROOT=`pwd`

readonly SERVER_ROOT="${TEST_TMPDIR}/server-root"

echo "Create test file"
mkdir -p "${SERVER_ROOT}"
cd "${SERVER_ROOT}"
cat > test_file.txt <<EOF
test
EOF

echo "Publish test file as local HTTP server"
# define location to store port number
port_file="${ROOT}/port.txt"
# start Python server as remote
python3 -u "${ROOT}/utils/run_test_server.py" "${port_file}" & server_pid=$!
# set up cleanup of http server
trap "server_cleanup ${server_pid}" INT TERM EXIT
# wait for the server to be available
tries=0
while [ -z "$(cat "${port_file}")" ] && [ $tries -lt 10 ]
do
    tries=$((${tries}+1))
    sleep 1s
done
if [ -z "$(cat ${port_file})" ]; then
    exit 1
fi

cd "${ROOT}"

echo "Run curl usage test"
error=false
test/other_tools/utils/curl_usage_install & res=$!
wait $res
if [ $? -ne 0 ]; then
    error=true
fi

# check test status
if [ $error = true ]; then
    exit 1
fi
