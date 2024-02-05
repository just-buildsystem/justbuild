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

set -e

readonly ROOT="$(pwd)"
readonly LBRDIR="$TMPDIR/local-build-root"
readonly JUST="${ROOT}/bin/tool-under-test"

mkdir -p "${ROOT}/rules"
cd "${ROOT}/rules"
touch ROOT
cat > RULES <<'EOI'
{ "":
  { "config_vars": ["TIMEOUT"]
  , "expression":
    { "type": "RESULT"
    , "artifacts":
      { "type": "ACTION"
      , "outs": ["out.txt"]
      , "cmd": ["sh", "-c", "echo Hello > out.txt\n"]
      , "timeout scaling": {"type": "var", "name": "TIMEOUT"}
      }
    }
  }
}
EOI
cat > TARGETS <<'EOI'
{ "flexible": {"type": ""}
, "fixed":
  { "type": "configure"
  , "target": "flexible"
  , "config": {"type": "singleton_map", "key": "TIMEOUT", "value": 1.0}
  }
, "unset":
  { "type": "configure"
  , "target": "flexible"
  , "config": {"type": "singleton_map", "key": "TIMEOUT", "value": null}
  }
, "":
  { "type": "install"
  , "files": {"flexible": "flexible", "fixed": "fixed", "unset": "unset"}
  }
}
EOI

mkdir -p "${ROOT}/generic"
cd "${ROOT}/generic"
touch ROOT
cat > TARGETS <<'EOI'
{ "flexible":
  { "type": "generic"
  , "arguments_config": ["TIMEOUT"]
  , "outs": ["out.txt"]
  , "cmds": ["echo Hello > out.txt"]
  , "timeout scaling": {"type": "var", "name": "TIMEOUT"}
  }
, "fixed":
  { "type": "configure"
  , "target": "flexible"
  , "config": {"type": "singleton_map", "key": "TIMEOUT", "value": 1.0}
  }
, "unset":
  { "type": "configure"
  , "target": "flexible"
  , "config": {"type": "singleton_map", "key": "TIMEOUT", "value": null}
  }
, "":
  { "type": "install"
  , "files": {"flexible": "flexible", "fixed": "fixed", "unset": "unset"}
  }
}
EOI


for variant in rules generic
do
echo "Testing variant ${variant}"
echo
cd "${ROOT}/${variant}"

"${JUST}" analyse --local-build-root "${LBRDIR}" --dump-graph graph-null.json 2>&1
cat graph-null.json | jq -acM '.actions | [ .[] | .origins | [ .[] | .config.TIMEOUT ]] | sort' > action_configs
cat action_configs
[ "$(jq -acM '. == [[1,null]]' action_configs)" = "true" ]

echo
"${JUST}" analyse --local-build-root "${LBRDIR}" -D '{"TIMEOUT": 1.0}' --dump-graph graph-1.json 2>&1
cat graph-1.json | jq -acM '.actions | [ .[] | .origins | [ .[] | .config.TIMEOUT ]] | sort' > action_configs
cat action_configs
[ "$(jq -acM '. == [[1,null]]' action_configs)" = "true" ]

echo
"${JUST}" analyse --local-build-root "${LBRDIR}" -D '{"TIMEOUT": 2.0}' --dump-graph graph-2.json 2>&1
cat graph-2.json
cat graph-2.json | jq -acM '.actions | [ .[] | .origins | [ .[] | .config.TIMEOUT ]] | sort' > action_configs
cat action_configs
[ "$(jq -acM '. == [[1,null],[2]]' action_configs)" = "true" ]

echo "variant ${variant} OK"
echo
done

# also verify the consistency of the generated actions
for graph in graph-null.json graph-1.json graph-2.json
do
    diff -u "${ROOT}/rules/${graph}" "${ROOT}/generic/${graph}"
done

echo OK
