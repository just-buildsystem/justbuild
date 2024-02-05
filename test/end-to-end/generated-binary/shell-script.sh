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
{ "script-hello":
  { "type": "generic"
  , "outs": ["script.sh"]
  , "cmds":
    [ "echo '#!/bin/sh' > script.sh"
    , "echo 'echo Hello World' >> script.sh"
    , "chmod 755 script.sh"
    ]
  }
, "generated-hello":
  { "type": "generic"
  , "outs": ["out-hello.txt"]
  , "deps": ["script-hello"]
  , "cmds": ["./script.sh > out-hello.txt"]
  }
, "script-morning":
  { "type": "generic"
  , "outs": ["script.sh"]
  , "cmds":
    [ "echo '#!/bin/sh' > script.sh"
    , "echo 'echo Good morning' >> script.sh"
    , "chmod 755 script.sh"
    ]
  }
, "generated-morning":
  { "type": "generic"
  , "outs": ["out-morning.txt"]
  , "deps": ["script-morning"]
  , "cmds": ["./script.sh > out-morning.txt"]
  }
, "ALL":
  {"type": "generic"
  , "deps": ["generated-hello", "generated-morning"]
  , "outs": ["out.txt"]
  , "cmds": ["cat out-*.txt > out.txt"]
  }
}
EOI


bin/tool-under-test install -L '["env", "PATH='"${PATH}"'"]' -o out --local-build-root .tool-root 2>&1
grep Hello out/out.txt
grep Good out/out.txt
