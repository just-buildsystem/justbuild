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

set -e

readonly ROOT="$(pwd)"
readonly LBRDIR="${TMPDIR}/local-build-root"
readonly JUST="${ROOT}/bin/tool-under-test"
readonly OUT="${ROOT}/out"
readonly LOCAL_BIN="${TMPDIR}/local-bin"
mkdir -p "${LOCAL_BIN}"
export PATH="${LOCAL_BIN}:${PATH}"

REMOTE_ARGS="-r ${REMOTE_EXECUTION_ADDRESS}"
if [ -n "${COMPATIBLE:-}" ]
then
    REMOTE_ARGS="${REMOTE_ARGS} --compatible"
fi
echo Using ${REMOTE_ARGS}


# To see what was built where, implement a command generateRoot differently
# locally and remotely.
cat > "${REMOTE_BIN}/generateRoot" <<'EOF'
#!/bin/sh
cat <<'EOI'
{ "":
  { "type": "generic"
  , "outs": ["out"]
  , "cmds": ["echo I was defined remotely > out"]
  }
}
EOI
EOF
chmod 755 "${REMOTE_BIN}/generateRoot"
cat > "${LOCAL_BIN}/generateRoot" <<'EOF'
#!/bin/sh
cat <<'EOI'
{ "":
  { "type": "generic"
  , "outs": ["out"]
  , "cmds": ["echo I was defined locally > out"]
  }
}
EOI
EOF
chmod 755 "${LOCAL_BIN}/generateRoot"



# The base root simply refers to the command generateRoot.

readonly BASE_ROOT="${ROOT}/base"
mkdir -p "${BASE_ROOT}"
cd "${BASE_ROOT}"
cat > TARGETS <<'EOF'
{ "": {"type": "export", "target": "root"}
, "root":
  {"type": "generic", "outs": ["TARGETS"], "cmds": ["generateRoot > TARGETS"]}
}
EOF
git init 2>&1
git branch -m stable-1.0 2>&1
git config user.email "nobody@example.org" 2>&1
git config user.name "Nobody" 2>&1
git add . 2>&1
git commit -m "Initial commit" 2>&1
GIT_TREE=$(git log -n 1 --format="%T")

mkdir -p "${ROOT}/main"
cd "${ROOT}/main"

cat > repo-config.json <<EOF
{ "repositories":
  { "base":
    {"workspace_root": ["git tree", "${GIT_TREE}", "${BASE_ROOT}/.git"]}
  , "derived":
    {"workspace_root": ["computed", "base", "", "", {}]}
  }
}
EOF
cat repo-config.json

echo
echo Building locally, to get a local target-cache entry
echo
"${JUST}" build -L '["env", "PATH='"${PATH}"'"]' \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    --main base --log-limit 4 -f "${OUT}/base-local.log" \
    -P TARGETS 2>&1
echo
# Now at least the action should be in cache, so the local
# tool should not be used any more
rm "${LOCAL_BIN}/generateRoot"

echo
echo Verifying local build
echo
"${JUST}" install -o "${OUT}/derived-local" \
    -L '["env", "PATH='"${PATH}"'"]' \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    --main derived --log-limit 4 -f "${OUT}/derived-local.log" 2>&1
echo
grep 'locally' "${OUT}/derived-local/out"

echo
echo Build locally again, to be sure the computed root is in cache
echo
"${JUST}" install -o "${OUT}/derived-local2" \
    -L '["env", "PATH='"${PATH}"'"]' \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    --main derived --log-limit 4 -f "${OUT}/derived-local2.log" 2>&1
echo
grep 'locally' "${OUT}/derived-local2/out"


echo
echo Verify remote build
echo
"${JUST}" install ${REMOTE_ARGS} -o "${OUT}/derived-remote" \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    --main derived --log-limit 4 -f "${OUT}/derived-remote.log" 2>&1
echo
grep 'remotely' "${OUT}/derived-remote/out"
echo
# Now, the remote entry is in cache, hence the remote tool should
# no longer be used.
rm "${REMOTE_BIN}/generateRoot"

echo
echo Verify remote build, again, ensuring the correct cache is used
echo
"${JUST}" install ${REMOTE_ARGS} -o "${OUT}/derived-remote2" \
    --local-build-root "${LBRDIR}" -C repo-config.json \
    --main derived --log-limit 4 -f "${OUT}/derived-remote2.log" 2>&1
echo
grep 'remotely' "${OUT}/derived-remote2/out"

echo OK
