#!/bin/sh
# Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

mkdir .tool-root
touch ROOT
cat > TARGETS <<'EOI'
{ "foo":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["echo Hello World > out.txt"]
  }
, "bar":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["echo Hello World > out.txt"]
  }
, "baz":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["echo -n Hello > out.txt && echo ' World' >> out.txt"]
  }
, "foo upper":
  { "type": "generic"
  , "deps": ["foo"]
  , "outs": ["upper.txt"]
  , "cmds": ["cat out.txt | tr a-z A-Z > upper.txt"]
  }
, "bar upper":
  { "type": "generic"
  , "deps": ["bar"]
  , "outs": ["upper.txt"]
  , "cmds": ["cat out.txt | tr a-z A-Z > upper.txt"]
  }
, "baz upper":
  { "type": "generic"
  , "deps": ["baz"]
  , "outs": ["upper.txt"]
  , "cmds": ["cat out.txt | tr a-z A-Z > upper.txt"]
  }
, "ALL":
  { "type": "install"
  , "files":
    {"foo.txt": "foo upper", "bar.txt": "bar upper", "baz.txt": "baz upper"}
  }
}
EOI


bin/tool-under-test build -J 1 --local-build-root .tool-root -f build.log --log-limit 2 2>&1
cat build.log
echo
grep 'Processed.* 4 actions' build.log
grep '1 cache hit' build.log

echo
bin/tool-under-test analyse --local-build-root .tool-root --dump-graph graph.json 2>&1
echo
matching_targets=$(cat graph.json | jq -acM '.actions | [ .[] | .origins | [ .[] | .target]] | sort')
echo "${matching_targets}"
[ "${matching_targets}" = '[[["@","","","bar"],["@","","","foo"]],[["@","","","bar upper"],["@","","","foo upper"]],[["@","","","baz"]],[["@","","","baz upper"]]]' ]
