#!/bin/sh
# Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

env

readonly ROOT="${PWD}"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly RCFILE="${TEST_TMPDIR}/mrrc.json"
readonly OUT="${TEST_TMPDIR}/out"
readonly LOCAL_REPO="${TEST_TMPDIR}/local-repository"

mkdir -p "${LOCAL_REPO}"
cd "${LOCAL_REPO}"
mkdir src
for c in a b c d e f g
do
    echo $c > src/$c.txt
done
git init
git config user.name 'N.O.Body'
git config user.email 'nobody@example.org'
git add -f .
git commit -m 'Test repo' 2>&1
COMMIT_1=$(git log -n 1 --format="%H")

mkdir -p "${ROOT}/work"
cd "${ROOT}/work"
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "A":
    { "repository":
      { "type": "git"
      , "commit": "$COMMIT_0"
      , "repository": "http://non-existent.example.org/data.git"
      , "branch": "master"
      , "subdir": "src"
      }
    , "target_root": ""
    , "target_file_name": "TARGETS.A"
    }
  , "B":
    { "repository":
      { "type": "git"
      , "commit": "$COMMIT_1"
      , "repository": "${LOCAL_REPO}"
      , "branch": "master"
      , "subdir": "src"
      , "pragma": {"absent": true}
      }
    , "target_root": ""
    , "target_file_name": "TARGETS.B"
    }
  , "":
    { "repository": {"type": "file", "path": "main"}
    , "bindings": {"A": "A", "B": "B"}
    }
  }
}
EOF
mkdir main
cat > main/TARGETS.A <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["outa.txt"]
  , "cmds": ["head -c 1 4.txt > outa.txt", "cat 2.txt >> outa.txt"]
  , "deps": ["4.txt", "2.txt"]
  }
}
EOF
cat > main/TARGETS.B <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["outb.txt"]
  , "cmds": ["head -c 1 e.txt > outb.txt", "cat g.txt >> outb.txt"]
  , "deps": ["e.txt", "g.txt"]
  }
}
EOF
cat > main/TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["cat outa.txt outb.txt > out.txt"]
  , "deps": [["@", "A", "", ""], ["@", "B", "", ""]]
  }
}
EOF
mkdir etc
cat > etc/absent.json <<'EOF'
["A"]
EOF
cat > "${RCFILE}" <<'EOF'
{"absent": [{"root": "workspace", "path": "etc/absent.json"}]}
EOF

echo
echo
CONF=$("${JUST_MR}" --local-build-root "${LBR}" \
                    --rc "${RCFILE}" \
                    --remote-serve-address ${SERVE} \
                    -r ${REMOTE_EXECUTION_ADDRESS} \
                    --fetch-absent setup)
cat $CONF
echo
"${JUST}" install --local-build-root "${LBR}" -C "${CONF}" \
          -r "${REMOTE_EXECUTION_ADDRESS}" -o "${OUT}" 2>&1
grep 42 "${OUT}/out.txt"
grep eg "${OUT}/out.txt"

echo DONE
