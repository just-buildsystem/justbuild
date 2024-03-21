#!/bin/bash
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

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TMPDIR}/local-build-root"
readonly LOG="${TMPDIR}/log.txt"

touch ROOT
cat <<'EOF' > repos.json
{ "repositories":
  { "":
    {"repository": {"type": "file", "path": ".", "pragma": {"to_git": true}}}
  }
}
EOF

cat <<'EOF' > TARGETS
{ "greeting":
  { "type": "generic"
  , "arguments_config": ["NAME"]
  , "outs": ["greeting.txt"]
  , "cmds":
    [ { "type": "join"
      , "$1":
        [ { "type": "join_cmd"
          , "$1":
            [ "echo"
            , "Hello"
            , {"type": "var", "name": "NAME", "default": "world"}
            ]
          }
        , " > greeting.txt"
        ]
      }
    ]
  }
, "exported":
  {"type": "export", "flexible_config": ["NAME"], "target": "greeting"}
, "world":
  { "type": "configure"
  , "target": "exported"
  , "config":
    { "type": "let*"
    , "bindings": [["NAME", "World"], ["unrelated", "foo"]]
    , "body": {"type": "env", "vars": ["NAME", "unrelated"]}
    }
  }
, "world-2":
  { "type": "configure"
  , "target": "exported"
  , "config":
    { "type": "let*"
    , "bindings": [["NAME", "World"], ["unrelated", "bar"]]
    , "body": {"type": "env", "vars": ["NAME", "unrelated"]}
    }
  }
, "universe":
  { "type": "configure"
  , "target": "exported"
  , "config":
    { "type": "let*"
    , "bindings": [["NAME", "Universe"], ["unrelated", "foo"]]
    , "body": {"type": "env", "vars": ["NAME", "unrelated"]}
    }
  }
, "":
  { "type": "install"
  , "files": {"world": "world", "world-2": "world-2", "universe": "universe"}
  }
}
EOF

# The export target is analysed in 3 configuration, but only two
# are different in the relevant part of the configuration; so
# we should see two uncached export targets.
"${JUST_MR}" --just "${JUST}" --local-build-root "${LBR}" \
             build -f "${LOG}" --log-limit 4 2>&1
echo
grep 'xport.*2 uncached' "${LOG}"
echo

# After building once, the export targets should be cached.
"${JUST_MR}" --just "${JUST}" --local-build-root "${LBR}" \
             build -f "${LOG}" --log-limit 4 2>&1
echo
grep 'xport.*2 cached' "${LOG}"
echo

echo OK
