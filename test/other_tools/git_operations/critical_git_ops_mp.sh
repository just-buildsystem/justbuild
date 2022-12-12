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

# Run the critical git ops test multiple times from different processes
echo "running critical git ops test from 4 processes"
error=false
test/other_tools/git_operations/critical_git_ops_test & res1=$!
test/other_tools/git_operations/critical_git_ops_test & res2=$!
test/other_tools/git_operations/critical_git_ops_test & res3=$!
test/other_tools/git_operations/critical_git_ops_test & res4=$!
wait $res1
if [ $? -ne 0 ]; then
    error=true
fi
wait $res2
if [ $? -ne 0 ]; then
    error=true
fi
wait $res3
if [ $? -ne 0 ]; then
    error=true
fi
wait $res4
if [ $? -ne 0 ]; then
    error=true
fi
# if one fails, set fail overall
if [ $error = true ]; then
    exit 1
fi
echo "done"
