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


mkdir repo_A
echo 'A' > repo_A/data.txt
cat > repo_A/TARGETS <<'EOF'
{ "hello":
  { "type": "generic"
  , "deps": [ "data.txt" ]
  , "outs": [ "hello.txt" ]
  , "cmds": [ "echo Hello `cat data.txt` > hello.txt" ]
  }
, "use":
  { "type": "generic"
  , "deps": [ [ "@" , "other" , "." , "hello" ] ]
  , "outs": [ "use.txt" ]
  , "cmds":
    [ "cat hello.txt > use.txt"
    , "echo This is A >> use.txt"
    ]
  }
, "back":
  {"type": "generic"
  , "deps": [ ["@", "other", ".", "use"] ]
  , "outs": [ "back.txt"]
  , "cmds" : [ "echo I am A and I see the following file > back.txt"
             , "echo >> back.txt"
             , "cat use.txt >> back.txt"
             ]
  }
}
EOF

mkdir repo_B
echo 'B' > repo_B/data.txt
cat > repo_B/TARGETS <<'EOF'
{ "hello":
  { "type": "generic"
  , "deps": [ "data.txt" ]
  , "outs": [ "hello.txt" ]
  , "cmds": [ "echo Hello `cat data.txt` > hello.txt" ]
  }
, "use":
  { "type": "generic"
  , "deps": [ [ "@" , "other" , "." , "hello" ] ]
  , "outs": [ "use.txt" ]
  , "cmds":
    [ "cat hello.txt > use.txt"
    , "echo This is B >> use.txt"
    ]
  }
, "back":
  {"type": "generic"
  , "deps": [ ["@", "other", ".", "use"] ]
  , "outs": [ "back.txt"]
  , "cmds" : [ "echo I am B and I see the following file > back.txt"
             , "echo >> back.txt"
             , "cat use.txt >> back.txt"
             ]
  }
}
EOF

cat > bindings.json <<'EOF'
{ "repositories" : {"A" : { "workspace_root": ["file", "repo_A"]
                          , "bindings": {"other": "B"}
                          }
                   , "B": { "workspace_root": ["file", "repo_B"]
                          , "bindings": {"other": "A"}
                          }
                   }
}
EOF

mkdir -p .root

echo == Building in A ==
./bin/tool-under-test install -C bindings.json -o . --local-build-root .root --main A . back 2>&1
cat back.txt
grep -q 'I am A' back.txt
grep -q 'This is B' back.txt
grep -q 'Hello A' back.txt
rm -f back.txt

echo == Building in B ==
./bin/tool-under-test install -C bindings.json -o . --local-build-root .root --main B . back 2>&1
cat back.txt
grep -q 'I am B' back.txt
grep -q 'This is A' back.txt
grep -q 'Hello B' back.txt
