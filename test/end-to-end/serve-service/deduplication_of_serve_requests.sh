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

set -eu

env
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly LOG="${TEST_TMPDIR}/log.txt"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

mkdir work
cd work
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"transform": "transform"}
    }
  , "transform":
    { "repository":
      { "type": "git"
      , "commit": "$COMMIT_0"
      , "pragma": {"absent": true}
      , "repository": "http://non-existent.example.org/data.git"
      , "branch": "master"
      }
    }
  }
}
EOF
cat repos.json


cat > TARGETS <<'EOF'
{ "": {"type": "install", "files": {"a-1": "a-1", "a-2": "a-2", "b": "b"}}
, "a-1":
  { "type": "configure"
  , "target": ["@", "transform", "", ""]
  , "config":
    { "type": "let*"
    , "bindings": [["DATA", "a"], ["unrelated", "foo"]]
    , "body": {"type": "env", "vars": ["DATA", "unrelated"]}
    }
  }
, "a-2":
  { "type": "configure"
  , "target": ["@", "transform", "", ""]
  , "config":
    { "type": "let*"
    , "bindings": [["DATA", "a"], ["unrelated", "bar"]]
    , "body": {"type": "env", "vars": ["DATA", "unrelated"]}
    }
  }
, "b":
  { "type": "configure"
  , "target": ["@", "transform", "", ""]
  , "config":
    { "type": "let*"
    , "bindings": [["DATA", "b"], ["unrelated", "foo"]]
    , "body": {"type": "env", "vars": ["DATA", "unrelated"]}
    }
  }
}
EOF

# As from the 3 absent export targets two coincide on the flexible
# variables, we should only get two export targets served.
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
             -R "${SERVE}" -r "${REMOTE_EXECUTION_ADDRESS}" ${COMPAT} \
             build -f "${LOG}" --log-limit 4 2>&1
echo
grep 'xport.*2 served' "${LOG}"
echo

# The same should be true on the second run, when everything is in
# the cache of serve.
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
             -R "${SERVE}" -r "${REMOTE_EXECUTION_ADDRESS}" ${COMPAT} \
             build -f "${LOG}" --log-limit 4 2>&1
echo
grep 'xport.*2 served' "${LOG}"
echo

echo OK
