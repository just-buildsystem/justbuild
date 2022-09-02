#!/bin/sh

set -eu

readonly JUST="$PWD/bin/tool-under-test"
readonly JUST_MR="$PWD/bin/just-mr.py"
readonly LBRDIR="$TEST_TMPDIR/local-build-root"
readonly TESTDIR="$TEST_TMPDIR/test-root"

# create project files including an exported target
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

# create random string: p<hostname>p<time[ns]>p<pid>p<random number[9 digits]>
readonly LOW=100000000
readonly HIGH=999999999
readonly RND="p$(hostname)p$(date +%s%N)p$$p$(shuf -i $LOW-$HIGH -n 1)"

cat > TARGETS <<EOF
{ "main": {"type": "export", "target": ["./", "main-target"]}
, "main-target":
  { "type": "generic"
  , "cmds": ["echo $RND | tee foo.txt out/bar.txt"]
  , "outs": ["foo.txt"]
  , "out_dirs": ["out"]
  }
}
EOF

ARGS=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  ARGS="--compatible"
fi

# build project twice locally to trigger a target cache hit
export CONF="$("$JUST_MR" -C repos.json --local-build-root="$LBRDIR" setup main)"
"$JUST" build -C "$CONF" main --local-build-root="$LBRDIR" $ARGS 2>&1
"$JUST" build -C "$CONF" main --local-build-root="$LBRDIR" $ARGS 2>&1

REMOTE_EXECUTION_ARGS=""
if [ "${REMOTE_EXECUTION_ADDRESS:-}" != "" ]; then
  REMOTE_EXECUTION_ARGS="-r $REMOTE_EXECUTION_ADDRESS"
  if [ "${REMOTE_EXECUTION_PROPERTIES:-}" != "" ]; then
    REMOTE_EXECUTION_ARGS="$REMOTE_EXECUTION_ARGS --remote-execution-property $REMOTE_EXECUTION_PROPERTIES"
  fi
fi

# build project twice remotely to trigger a target cache hit
"$JUST" build -C "$CONF" main --local-build-root="$LBRDIR" $ARGS $REMOTE_EXECUTION_ARGS 2>&1
"$JUST" build -C "$CONF" main --local-build-root="$LBRDIR" $ARGS $REMOTE_EXECUTION_ARGS 2>&1
