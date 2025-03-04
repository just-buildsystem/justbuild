#!/bin/sh
# Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

###
# This test checks that we correctly shard the target cache. We do this by
# building the same generic target in execution endpoint-specific environments
# via the launcher argument and checking that we get the expected caching of
# the target and the correct action output.
##

set -eu

env

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"

readonly LBR="${TEST_TMPDIR}/lbr"

readonly WRKDIR="${PWD}/work"

# local paths
readonly LOCAL_ROOT="${TEST_TMPDIR}/local"
readonly LOCAL_BIN_DIR="${LOCAL_ROOT}/bin"

# remote paths
readonly REMOTE_ROOT="${TEST_TMPDIR}/remote"
readonly REMOTE_CACHE="${REMOTE_ROOT}/build-root"
readonly REMOTE_INFOFILE="${REMOTE_ROOT}/info.json"
readonly REMOTE_PIDFILE="${REMOTE_ROOT}/pid.txt"
readonly REMOTE_BIN_DIR="${REMOTE_ROOT}/bin"

# serve standalone paths
readonly SERVE_ROOT="${TEST_TMPDIR}/serve"
readonly SERVE_CACHE="${SERVE_ROOT}/build-root"
readonly SERVE_INFOFILE="${SERVE_ROOT}/info.json"
readonly SERVE_PIDFILE="${SERVE_ROOT}/pid.txt"
readonly SERVE_BIN_DIR="${SERVE_ROOT}/bin"

# serve using remote paths
readonly SERVE_RE_ROOT="${TEST_TMPDIR}/serve-remote"
readonly SERVE_RE_CACHE="${SERVE_RE_ROOT}/build-root"
readonly SERVE_RE_INFOFILE="${SERVE_RE_ROOT}/info.json"
readonly SERVE_RE_PIDFILE="${SERVE_RE_ROOT}/pid.txt"
readonly SERVE_RE_BIN_DIR="${SERVE_RE_ROOT}/bin"

if [ "${COMPATIBLE:-}" = "YES" ]; then
  ARGS="--compatible"
  SERVE_COMPAT="true"
else
  ARGS=""
fi

##
# Set up just execute endpoint with custom launcher
#
mkdir -p "${REMOTE_BIN_DIR}"
cat > "${REMOTE_BIN_DIR}/endpoint" <<'EOF'
#!/bin/sh
echo remote > $1
EOF
chmod 755 "${REMOTE_BIN_DIR}/endpoint"

"${JUST}" execute --info-file "${REMOTE_INFOFILE}" --pid-file "${REMOTE_PIDFILE}" \
          --local-build-root "${REMOTE_CACHE}" \
          -L '["env", "PATH='"${REMOTE_BIN_DIR}:${PATH}"'"]' \
          ${ARGS} 2>&1 &

for _ in `seq 1 60`; do
  if test -f "${REMOTE_INFOFILE}"; then
    break
  fi
  sleep 1;
done

if ! test -f "${REMOTE_INFOFILE}"; then
  echo "Did not find ${REMOTE_INFOFILE}"
  exit 1
fi

REMOTE_ADDRESS="127.0.0.1:$(jq '."port"' "${REMOTE_INFOFILE}")"

##
# Set up standalone just serve endpoint with custom launcher
#
mkdir -p "${SERVE_BIN_DIR}"
cat > "${SERVE_BIN_DIR}/endpoint" <<'EOF'
#!/bin/sh
echo serve > $1
EOF
chmod 755 "${SERVE_BIN_DIR}/endpoint"

cat > .just-servec <<EOF
{ "repositories": []
, "remote service":
  { "info file": {"root": "system", "path": "${SERVE_INFOFILE}"}
  , "pid file": {"root": "system", "path": "${SERVE_PIDFILE}"}
  }
, "local build root": {"root": "system", "path": "${SERVE_CACHE}"}
, "build": {"local launcher": ["env", "PATH=${SERVE_BIN_DIR}:${PATH}"]}
EOF
if [ "${COMPATIBLE:-}" = "YES" ]
then
  cat >> .just-servec <<EOF
, "execution endpoint": {"compatible": true}
EOF
fi
cat >> .just-servec <<EOF
}
EOF
echo "Serve service configuration:"
cat .just-servec

"${JUST}" serve .just-servec 2>&1 &

for _ in `seq 1 60`
do
    if test -f "${SERVE_INFOFILE}"
    then
        break
    fi
    sleep 1;
done

if ! test -f "${SERVE_INFOFILE}"
then
    echo "Did not find ${SERVE_INFOFILE}"
    exit 1
fi

SERVE_ADDRESS="127.0.0.1:$(jq '."port"' "${SERVE_INFOFILE}")"

##
# Set up just serve endpoint with custom launcher that dispatches to remote
#
mkdir -p "${SERVE_RE_BIN_DIR}"
cat > "${SERVE_RE_BIN_DIR}/endpoint" <<'EOF'
#!/bin/sh
echo serve-remote > $1
EOF
chmod 755 "${SERVE_RE_BIN_DIR}/endpoint"

cat > .just-servec <<EOF
{ "repositories": []
, "remote service":
  { "info file": {"root": "system", "path": "${SERVE_RE_INFOFILE}"}
  , "pid file": {"root": "system", "path": "${SERVE_RE_PIDFILE}"}
  }
, "local build root": {"root": "system", "path": "${SERVE_RE_CACHE}"}
, "build": {"local launcher": ["env", "PATH=${SERVE_RE_BIN_DIR}:${PATH}"]}
, "execution endpoint":
  { "address": "${REMOTE_ADDRESS}"
EOF
if [ "${COMPATIBLE:-}" = "YES" ]
then
  cat >> .just-servec <<EOF
  , "compatible": true
EOF
fi
cat >> .just-servec <<EOF
  }
}
EOF
echo "Serve service configuration:"
cat .just-servec

"${JUST}" serve .just-servec 2>&1 &

for _ in `seq 1 60`
do
    if test -f "${SERVE_RE_INFOFILE}"
    then
        break
    fi
    sleep 1;
done

if ! test -f "${SERVE_RE_INFOFILE}"
then
    echo "Did not find ${SERVE_RE_INFOFILE}"
    exit 1
fi

SERVE_RE_ADDRESS="127.0.0.1:$(jq '."port"' "${SERVE_RE_INFOFILE}")"

##
# Set up remotes cleanup
#
cleanup() {
  kill $(cat "${REMOTE_PIDFILE}")
  kill $(cat "${SERVE_PIDFILE}")
  kill $(cat "${SERVE_RE_PIDFILE}")
}
trap cleanup EXIT

##
# Set up local launcher
#
mkdir -p "${LOCAL_BIN_DIR}"
cat > "${LOCAL_BIN_DIR}/endpoint" <<'EOF'
#!/bin/sh
echo local > $1
EOF
chmod 755 "${LOCAL_BIN_DIR}/endpoint"

LOCAL_LAUNCHER='["env", "PATH='"${LOCAL_BIN_DIR}:${PATH}"'"]'

##
# Set up the test target to build. Each execution endpoint will know its own
# version of the test script to run.
#
mkdir -p "${WRKDIR}/src"
cd "${WRKDIR}"

touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "file"
      , "path": "src"
      , "pragma": {"to_git": true}
      }
    }
  }
}
EOF
cat repos.json

cat > src/TARGETS <<EOF
{ "": {"type": "export", "target": "test"}
, "test":
  { "type": "generic"
  , "cmds": ["endpoint test.out"]
  , "outs": ["test.out"]
  }
}
EOF
cat src/TARGETS
echo

##
# Build the export target on the various execution endpoints.
# Check that we never get a target cache hit.
#
mkdir -p result

# Local build has the default TC shard.
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
  ${ARGS} -L "${LOCAL_LAUNCHER}" install -o result 2>&1
if ! grep local result/test.out; then
  echo 'Expected "local" result but found "'$(cat result/test.out)'"'
  exit 1
fi
echo

# A regular remote endpoint has some different local TC shard client-side than
# a local build.
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
  ${ARGS} -r ${REMOTE_ADDRESS} install -o result 2>&1
if ! grep remote result/test.out; then
  echo 'Expected "remote" result but found "'$(cat result/test.out)'"'
  exit 1
fi
echo

# A serve endpoint that dispatches to the same remote as before will have the
# same client-side TC shard as if it the build was directly dispatched to said
# remote.
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
  ${ARGS} -R ${SERVE_RE_ADDRESS} -r ${REMOTE_ADDRESS} install -o result 2>&1
if ! grep remote result/test.out; then
  echo 'Expected "remote" result but found "'$(cat result/test.out)'"'
  exit 1
fi
echo

# Serve standalone only shards like a local build on the serve-side.
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
  ${ARGS} -R ${SERVE_ADDRESS} install -o result 2>&1
if ! grep serve result/test.out; then
  echo 'Expected "serve" result but found "'$(cat result/test.out)'"'
  exit 1
fi
echo

# Serve standalone can also be used purely as a remote execution endpoint.
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
  ${ARGS} -r ${SERVE_ADDRESS} install -o result 2>&1
if ! grep serve result/test.out; then
  echo 'Expected "serve" result but found "'$(cat result/test.out)'"'
  exit 1
fi
echo

# Check that rebuilding locally after all these runs picks up the correct TC
# cache hit.
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
  ${ARGS} -L "${LOCAL_LAUNCHER}" install -o result 2>&1
if ! grep local result/test.out; then
  echo 'Expected "local" result but found "'$(cat result/test.out)'"'
  exit 1
fi
echo

# Check that removing all the binaries results in correct cache hits.
rm "${LOCAL_BIN_DIR}/endpoint"
rm "${REMOTE_BIN_DIR}/endpoint"
rm "${SERVE_BIN_DIR}/endpoint"
rm "${SERVE_RE_BIN_DIR}/endpoint"

"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
  ${ARGS} -L "${LOCAL_LAUNCHER}" install -o result 2>&1
if ! grep local result/test.out; then
  echo 'Expected "local" result but found "'$(cat result/test.out)'"'
  exit 1
fi
echo

"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
  ${ARGS} -r ${REMOTE_ADDRESS} install -o result 2>&1
if ! grep remote result/test.out; then
  echo 'Expected "remote" result but found "'$(cat result/test.out)'"'
  exit 1
fi
grep remote result/test.out
echo

"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
  ${ARGS} -R ${SERVE_RE_ADDRESS} -r ${REMOTE_ADDRESS} install -o result 2>&1
if ! grep remote result/test.out; then
  echo 'Expected "remote" result but found "'$(cat result/test.out)'"'
  exit 1
fi
echo

"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
  ${ARGS} -R ${SERVE_ADDRESS} install -o result 2>&1
if ! grep serve result/test.out; then
  echo 'Expected "serve" result but found "'$(cat result/test.out)'"'
  exit 1
fi
echo

"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
  ${ARGS} -r ${SERVE_ADDRESS} install -o result 2>&1
if ! grep serve result/test.out; then
  echo 'Expected "serve" result but found "'$(cat result/test.out)'"'
  exit 1
fi
echo

echo OK
