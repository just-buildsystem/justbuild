#!/bin/sh

set -eu

# This test requires remote execution and is skipped in case of local execution.
# The test is separated into two phases. In phase I, the execution IDs for the
# local and the remote execution backends are determined. In phase II, the
# actual test is executed, which is explained in the next section.

# In order to test the synchronization of target-level cached remote artifacts
# to the local CAS and back, the following test has been designed. The test
# needs to verify that if any artifact that is mentioned in a target cache entry
# is garbage collected on the remote side is actually uploaded by the
# synchronization mechanism and will be available for remote action execution.
# In order to "emulate" the behavior of a garbage collected file or tree on the
# remote side, a simple project with parts eligible for target-level caching is
# built locally and the target cache entry is pretended to be created for the
# remote execution backend. If then remote execution is triggered, the files or
# trees are not available on the remote side, so they need to be uploaded from
# the local CAS. After the artifacts have been uploaded to the remote CAS, they
# would be available for the next test run, thus, random strings are used in the
# affected text files to vary the according hash value.

readonly JUST="$PWD/bin/tool-under-test"
readonly JUST_MR="$PWD/bin/just-mr.py"
readonly LBRDIR="$TEST_TMPDIR/local-build-root"
readonly TESTDIR="$TEST_TMPDIR/test-root"

if [ "${REMOTE_EXECUTION_ADDRESS:-}" = "" ]; then
  echo
  echo "Test skipped, since no remote execution is specified."
  echo
else
  REMOTE_EXECUTION_ARGS="-r $REMOTE_EXECUTION_ADDRESS"

  if [ "${REMOTE_EXECUTION_PROPERTIES:-}" != "" ]; then
    REMOTE_EXECUTION_ARGS="$REMOTE_EXECUTION_ARGS --remote-execution-property $REMOTE_EXECUTION_PROPERTIES"
  fi

  if [ "${COMPATIBLE:-}" = "YES" ]; then
    ARGS="--compatible"
    TCDIR="$LBRDIR/protocol-dependent/compatible-sha256/tc"
  else
    ARGS=""
    TCDIR="$LBRDIR/protocol-dependent/git-sha1/tc"
  fi

  # create common test files
  mkdir -p "$TESTDIR"
  cd "$TESTDIR"
  touch ROOT
  cat > repos.json <<EOF
{ "repositories":
  { "main":
    {"repository": {"type": "file", "path": ".", "pragma": {"to_git": true}}}
  }
}
EOF

  # ----------------------------------------------------------------------------
  # Test Phase I
  # ----------------------------------------------------------------------------

  cat > TARGETS.p1 <<EOF
{ "main": {"type": "export", "target": ["./", "main-target"]}
, "main-target":
  {"type": "generic", "cmds": ["echo foo > foo.txt"], "outs": ["foo.txt"]}
}
EOF

  export CONF="$("$JUST_MR" -C repos.json --local-build-root="$LBRDIR" setup main)"

  # determine local execution ID
  "$JUST" build -C "$CONF" main --local-build-root="$LBRDIR" --target-file-name TARGETS.p1 $ARGS > /dev/null 2>&1
  readonly LOCAL_EXECUTION_ID=$(ls "$TCDIR")
  rm -rf "$TCDIR"

  # determine remote execution ID
  "$JUST" build -C "$CONF" main --local-build-root="$LBRDIR" --target-file-name TARGETS.p1 $ARGS $REMOTE_EXECUTION_ARGS > /dev/null 2>&1
  readonly REMOTE_EXECUTION_ID=$(ls "$TCDIR")
  rm -rf "$TCDIR"

  # ----------------------------------------------------------------------------
  # Test Phase II
  # ----------------------------------------------------------------------------

  # create random string: p<hostname>p<time[ns]>p<pid>p<random number[9 digits]>
  readonly LOW=100000000
  readonly HIGH=999999999
  readonly RND="p$(hostname)p$(date +%s%N)p$$p$(shuf -i $LOW-$HIGH -n 1)"

  cat > TARGETS.p2 <<EOF
{ "main": {"type": "export", "target": ["./", "main-target"]}
, "main-target":
  { "type": "generic"
  , "cmds": ["echo $RND | tee foo.txt out/bar.txt"]
  , "outs": ["foo.txt"]
  , "out_dirs": ["out"]
  }
}
EOF

  export CONF="$("$JUST_MR" -C repos.json --local-build-root="$LBRDIR" setup main)"

  # build project locally
  echo
  echo "Build project locally"
  echo
  "$JUST" build -C "$CONF" main --local-build-root="$LBRDIR" --target-file-name TARGETS.p2 $ARGS 2>&1

  # pretend target cache entry being created for remote execution backend
  mv "$TCDIR/$LOCAL_EXECUTION_ID" "$TCDIR/$REMOTE_EXECUTION_ID"

  # build project remotely
  echo
  echo "Build project remotely"
  echo
  "$JUST" build -C "$CONF" main --local-build-root="$LBRDIR" --target-file-name TARGETS.p2 $ARGS $REMOTE_EXECUTION_ARGS 2>&1
fi
