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

readonly LBRDIR="$TMPDIR/local-build-root"
readonly JUST="bin/tool-under-test"

touch ROOT
cat > RULES <<'EOI'
{ "":
  { "config_vars": ["PROPERTIES"]
  , "expression":
    { "type": "RESULT"
    , "artifacts":
      { "type": "ACTION"
      , "outs": ["out.txt"]
      , "cmd": ["sh", "-c", "echo Hello > out.txt"]
      , "execution properties": {"type": "var", "name": "PROPERTIES"}
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
  , "config":
    { "type": "singleton_map"
    , "key": "PROPERTIES"
    , "value": {"type": "empty_map"}
    }
  }
, "unset":
  { "type": "configure"
  , "target": "flexible"
  , "config": {"type": "singleton_map", "key": "PROPERTIES", "value": null}
  }
, "":
  { "type": "install"
  , "files": {"flexible": "flexible", "fixed": "fixed", "unset": "unset"}
  }
}
EOI

"${JUST}" analyse --local-build-root "${LBRDIR}" --dump-graph graph-null.json 2>&1
action_configs=$(cat graph-null.json | jq -acM '.actions | [ .[] | .origins | [ .[] | .config.PROPERTIES ]] | sort')
echo "${action_configs}"
[ "${action_configs}" = '[[null,{}]]' ]

echo
"${JUST}" analyse --local-build-root "${LBRDIR}" -D '{"PROPERTIES": {}}' --dump-graph graph-empty.json 2>&1
action_configs=$(cat graph-empty.json | jq -acM '.actions | [ .[] | .origins | [ .[] | .config.PROPERTIES ]] | sort')
echo "${action_configs}"
[ "${action_configs}" = '[[null,{}]]' ]

"${JUST}" analyse --local-build-root "${LBRDIR}" -D '{"PROPERTIES": {"foo": "bar"}}' --dump-graph graph-set.json 2>&1
action_configs=$(cat graph-set.json | jq -acM '.actions | [ .[] | .origins | [ .[] | .config.PROPERTIES ]] | sort')
echo "${action_configs}"
[ "${action_configs}" = '[[null,{}],[{"foo":"bar"}]]' ]

echo OK
