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
export TMPDIR="${TEST_TMPDIR}"

# Change to the working directory; note: while unlikely, the test
# might not have test data, so we have to ensure the presence of
# the work directory.
mkdir -p work
cd work

date +%s > ../time-start
# TODO:
# - proper wrapping with timeout
if sh ../test.sh > ../stdout 2> ../stderr
then
    RESULT=PASS
else
    RESULT=FAIL
fi
date +%s > ../time-stop

# Ensure all the promissed output files in the work directory
# are present, even if the test failed to create them.
for f in "$@"
do
    touch "./${f}"
done

echo "${RESULT}" > ../result

if [ "${RESULT}" '!=' PASS ]
then
   exit 1;
fi
