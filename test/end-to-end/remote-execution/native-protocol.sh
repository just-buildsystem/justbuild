#!/bin/sh

set -eu

readonly DIRNAME="dir"
readonly OUT_DIRNAME="${DIRNAME}out"
readonly GITDIR="${TEST_TMPDIR}/git-root"
readonly LBRDIR="${TEST_TMPDIR}/local-build-root"
readonly JUST_MR="${PWD}/bin/just-mr.py"
readonly JUST="${PWD}/bin/tool-under-test"
readonly RESULT="out.txt"

echo
echo Create Git repository
echo
mkdir -p "${GITDIR}"
cd "${GITDIR}"
git init
git config user.name "Nobody"
git config user.email "nobody@example.org"
mkdir -p "${DIRNAME}"
echo foo > "${DIRNAME}/foo.txt"
echo bar > "${DIRNAME}/bar.txt"
git add "${DIRNAME}"
git commit -m "Initial commit"
readonly GIT_COMMIT_ID="$(git log -n 1 --format=%H)"
readonly GIT_TREE_ID="$(git log -n 1 --format=%T)"

cat > repos.json <<EOF
{ "repositories":
  { "test":
    { "repository":
      { "type": "git"
      , "branch": "master"
      , "commit": "${GIT_COMMIT_ID}"
      , "repository": "${GITDIR}"
      }
    , "target_root": "test-targets-layer"
    }
  , "test-targets-layer": {"repository": {"type": "file", "path": "."}}
  }
}
EOF

cat > TARGETS <<EOF
{ "test":
  { "type": "generic"
  , "cmds": ["cp -r ${DIRNAME} ${OUT_DIRNAME}"]
  , "deps": [["TREE", null, "${DIRNAME}"]]
  , "out_dirs": ["${OUT_DIRNAME}"]
  }
}
EOF

readonly CONF="$("${JUST_MR}" -C repos.json --local-build-root="${LBRDIR}" setup 2>&1)"

NAME="native"
ARGS=""
EQUAL="="
if [ "${COMPATIBLE:-}" = "YES" ]; then
  NAME="compatible"
  ARGS="--compatible"
  EQUAL="!="
fi

echo
echo Upload and download Git tree to local CAS in ${NAME} mode
echo
"${JUST}" build -C "${CONF}" --main test test --local-build-root="${LBRDIR}" --dump-artifacts "${RESULT}" ${ARGS} 2>&1
TREE_ID="$(jq -r ".${OUT_DIRNAME}.id" "${RESULT}" 2>&1)"
test ${TREE_ID} ${EQUAL} ${GIT_TREE_ID}

if [ "${REMOTE_EXECUTION_ADDRESS:-}" != "" ]; then
  REMOTE_EXECUTION_ARGS="-r ${REMOTE_EXECUTION_ADDRESS}"
  if [ "${REMOTE_EXECUTION_PROPERTIES:-}" != "" ]; then
    REMOTE_EXECUTION_ARGS="${REMOTE_EXECUTION_ARGS} --remote-execution-property ${REMOTE_EXECUTION_PROPERTIES}"
  fi
  echo
  echo Upload and download Git tree to remote CAS in ${NAME} mode
  echo
  "${JUST}" build -C "${CONF}" --main test test ${REMOTE_EXECUTION_ARGS} --local-build-root="${LBRDIR}" --dump-artifacts "${RESULT}" ${ARGS} 2>&1
  TREE_ID="$(jq -r ".${OUT_DIRNAME}.id" "${RESULT}" 2>&1)"
  test ${TREE_ID} ${EQUAL} ${GIT_TREE_ID}
fi
