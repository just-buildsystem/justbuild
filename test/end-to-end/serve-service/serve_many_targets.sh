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
readonly OUT="${TEST_TMPDIR}/output-dir"

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
{ "":
  { "type": "install"
  , "files": {"a": "a", "b": "b", "c": "c", "d": "d", "e": "e", "f": "f"}
  }
, "a":
  { "type": "configure"
  , "target": ["@", "transform", "", ""]
  , "config": {"type": "singleton_map", "key": "DATA", "value": "a"}
  }
, "b":
  { "type": "configure"
  , "target": ["@", "transform", "", ""]
  , "config": {"type": "singleton_map", "key": "DATA", "value": "b"}
  }
, "c":
  { "type": "configure"
  , "target": ["@", "transform", "", ""]
  , "config": {"type": "singleton_map", "key": "DATA", "value": "c"}
  }
, "d":
  { "type": "configure"
  , "target": ["@", "transform", "", ""]
  , "config": {"type": "singleton_map", "key": "DATA", "value": "d"}
  }
, "e":
  { "type": "configure"
  , "target": ["@", "transform", "", ""]
  , "config": {"type": "singleton_map", "key": "DATA", "value": "e"}
  }
, "f":
  { "type": "configure"
  , "target": ["@", "transform", "", ""]
  , "config": {"type": "singleton_map", "key": "DATA", "value": "f"}
  }
}
EOF

"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" \
             -R "${SERVE}" -r "${REMOTE_EXECUTION_ADDRESS}" ${COMPAT} \
             install -o "${OUT}" 2>&1

[ "$(cat "${OUT}/a")" = "AAA" ]
[ "$(cat "${OUT}/b")" = "BBB" ]
[ "$(cat "${OUT}/c")" = "CCC" ]
[ "$(cat "${OUT}/d")" = "DDD" ]
[ "$(cat "${OUT}/e")" = "EEE" ]
[ "$(cat "${OUT}/f")" = "FFF" ]

echo OK
