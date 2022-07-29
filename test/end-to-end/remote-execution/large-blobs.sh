#!/bin/sh

set -eu

readonly JUST="${PWD}/bin/tool-under-test"

# create a sufficiently large (>4MB) file for testing upload/download (16MB)
dd if=/dev/zero of=large.file bs=1024 count=$((16*1024))

touch ROOT
cat > TARGETS <<EOF
{ "test":
  { "type": "generic"
  , "cmds": ["cp large.file out.file"]
  , "deps": [["FILE", null, "large.file"]]
  , "outs": ["out.file"]
  }
}
EOF

NAME="native"
COMMON_ARGS="--local-build-root=.root"
if [ "${COMPATIBLE:-}" = "YES" ]; then
  NAME="compatible"
  COMMON_ARGS="$COMMON_ARGS --compatible"
fi

run_tests() {
    local TYPE="local"
    local REMOTE_ARGS=""
    local REMOTE_BUILD_ARGS=""
    if [ -n "${1:-}" ] && [ -n "${2:-}" ]; then
        TYPE="remote"
        REMOTE_ARGS="-r $1"
        REMOTE_BUILD_ARGS="--remote-execution-property $2"
    fi
    ARGS="$COMMON_ARGS $REMOTE_ARGS"
    BUILD_ARGS="$ARGS $REMOTE_BUILD_ARGS"

    echo
    echo Upload and download to $NAME $TYPE CAS.
    echo
    "${JUST}" build test $BUILD_ARGS --dump-artifacts result
    echo SUCCESS

    local ARTIFACT_ID="$(cat result | jq -r '."out.file".id')"
    local ARTIFACT_SIZE="$(cat result | jq -r '."out.file".size')"

    echo
    echo Download from $NAME $TYPE CAS with full qualified object-id.
    echo
    rm -rf out.file
    "${JUST}" install-cas $ARGS -o out.file [$ARTIFACT_ID:$ARTIFACT_SIZE:f]
    cmp large.file out.file
    echo SUCCESS

    # omitting size is only supported in native mode or for local backend
    if [ "$NAME" = "native" ] || [ "$TYPE" = "local" ]; then
        echo
        echo Download from $NAME $TYPE CAS with missing size and file type.
        echo
        rm -rf out.file
        "${JUST}" install-cas $ARGS -o out.file $ARTIFACT_ID
        cmp large.file out.file
        echo SUCCESS
    fi
}

run_tests #local
run_tests "${REMOTE_EXECUTION_ADDRESS:-}" "${REMOTE_EXECUTION_PROPERTIES:-}"
