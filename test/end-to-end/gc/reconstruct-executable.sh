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

readonly JUST="${PWD}/bin/tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly JUST_MR_ARGS="--norc --local-build-root ${LBR} --just ${JUST}"

mkdir repo
cd repo
touch ROOT
cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": "."}}}}
EOF

cat > generate-data.py <<'EOF'
#!/usr/bin/env python3

# Generates a C source file where we know the binary
# will be larger than the compactification threshold, as
# it contains enough data embedded.

import random
import sys

count = 4 * 1024 * 1024

random.seed(sys.argv[1])

print("#include <stdio.h>")
print("int main(int argc, char **argv) {")
print("   char data[%d] = {" % (count+1,))
bytes_to_embed = random.randbytes(count);
for c in bytes_to_embed:
    print("        '\\x%02x'," %(c,))
print("        '\\x00'};");
print("    int i;")
print("    for (i=0; i < %d; i++) {" % (count,));
print("      printf(\"%c\", data[i]);")
print("    }")
print("    return 0;")
print("}")
EOF
chmod 755 generate-data.py

# The default target depends on many large binaries that need to be run
cat > TARGETS <<'EOF'
{ "source":
  { "type": "generic"
  , "arguments_config": ["SEED"]
  , "outs": ["src.c"]
  , "deps": ["generate-data.py"]
  , "cmds":
    [ { "type": "join"
      , "$1":
        [ { "type": "join_cmd"
          , "$1": ["./generate-data.py", {"type": "var", "name": "SEED"}]
          }
        , "> src.c"
        ]
      }
    ]
  }
, "binary":
  { "type": "generic"
  , "outs": ["generate"]
  , "deps": ["source"]
  , "cmds": ["cc -o generate src.c"]
  }
, "data":
  { "type": "generic"
  , "outs": ["data"]
  , "deps": ["binary"]
  , "cmds": ["./generate > data"]
  }
, "aaa":
  { "type": "configure"
  , "target": "data"
  , "config": {"type": "singleton_map", "key": "SEED", "value": "aaa"}
  }
, "bbb":
  { "type": "configure"
  , "target": "data"
  , "config": {"type": "singleton_map", "key": "SEED", "value": "bbb"}
  }
, "ccc":
  { "type": "configure"
  , "target": "data"
  , "config": {"type": "singleton_map", "key": "SEED", "value": "ccc"}
  }
, "ddd":
  { "type": "configure"
  , "target": "data"
  , "config": {"type": "singleton_map", "key": "SEED", "value": "ddd"}
  }
, "eee":
  { "type": "configure"
  , "target": "data"
  , "config": {"type": "singleton_map", "key": "SEED", "value": "eee"}
  }
, "fff":
  { "type": "configure"
  , "target": "data"
  , "config": {"type": "singleton_map", "key": "SEED", "value": "fff"}
  }
, "":
  { "type": "install"
  , "files":
    { "aaa": "aaa"
    , "bbb": "bbb"
    , "ccc": "ccc"
    , "ddd": "ddd"
    , "eee": "eee"
    , "fff": "fff"
    }
  }
}
EOF

# First we build all the relevant binaries to have them in test
"${JUST_MR}" ${JUST_MR_ARGS} build -L '["env", "PATH='"${PATH}"'"]' -D '{"SEED": "aaa"}' binary 2>&1
"${JUST_MR}" ${JUST_MR_ARGS} build -L '["env", "PATH='"${PATH}"'"]' -D '{"SEED": "bbb"}' binary 2>&1
"${JUST_MR}" ${JUST_MR_ARGS} build -L '["env", "PATH='"${PATH}"'"]' -D '{"SEED": "ccc"}' binary 2>&1
"${JUST_MR}" ${JUST_MR_ARGS} build -L '["env", "PATH='"${PATH}"'"]' -D '{"SEED": "ddd"}' binary 2>&1
"${JUST_MR}" ${JUST_MR_ARGS} build -L '["env", "PATH='"${PATH}"'"]' -D '{"SEED": "eee"}' binary 2>&1
"${JUST_MR}" ${JUST_MR_ARGS} build -L '["env", "PATH='"${PATH}"'"]' -D '{"SEED": "fff"}' binary 2>&1

# Now, compactify
"${JUST_MR}" ${JUST_MR_ARGS} gc --no-rotate

# Finally, run the default target; this will, in parallel, reconstruct the
# compactified binaries and use them
"${JUST_MR}" ${JUST_MR_ARGS} build -L '["env", "PATH='"${PATH}"'"]' -J20 2>&1

echo
echo OK
