#!/bin/sh

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
if ../test > ../stdout 2> ../stderr
then
    RESULT=PASS
else
    RESULT=FAIL
fi
date +%s > ../time-stop
echo "${RESULT}" > result

if [ "${RESULT}" '!=' PASS ]
then
   exit 1;
fi
