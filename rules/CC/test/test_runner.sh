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


# ensure all required outputs are present
touch stdout
touch stderr
RESULT=UNKNOWN
echo "${RESULT}" > result
echo UNKNOWN > time-start
echo UNKNOWN > time-stop

mkdir scratch
export TEST_TMPDIR=$(realpath scratch)
# Change to the working directory; note: the test might not
# have test data, so we have to ensure the presence of the work
# directory.

mkdir -p work
cd work

date +%s > ../time-start
# TODO:
# - proper wrapping with timeout
# - test arguments to select specific test cases
if "$@" ../test > ../stdout 2> ../stderr
then
    RESULT=PASS
else
    RESULT=FAIL
fi
date +%s > ../time-stop
echo "${RESULT}" > ../result

if [ "${RESULT}" '!=' PASS ]
then
   exit 1;
fi
