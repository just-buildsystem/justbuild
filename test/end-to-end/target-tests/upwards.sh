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

mkdir -p .root

mkdir -p  src
touch src/ROOT
echo BAD > outside.txt

# Target names that look like upwards references are OK

echo "== legitimate target =="

cat > src/TARGETS <<'EOF'
{ "it":
  { "type": "generic"
  , "cmds": ["cat *.txt > out"]
  , "outs": ["out"]
  , "deps": ["../outside.txt"]
  }
, "../outside.txt":
  {"type": "file_gen", "name": "inside.txt", "data": "Everything OK"}
}
EOF

./bin/tool-under-test install -o out --workspace-root src \
	-L '["env", "PATH='"${PATH}"'"]' \
	--local-build-root .root . it 2>&1

grep OK out/out
grep BAD out/out && exit 1 || :

# Upwards references in relative target names are OK;
# continue the previous test case

echo "== legitimate upwards target reference =="

mkdir -p src/deep
cat > src/deep/TARGETS <<'EOF'
{ "OK up":
  { "type": "generic"
  , "cmds": ["cat *.txt > out"]
  , "outs": ["out"]
  , "deps": [ ["./", "..", "../outside.txt"] ]
  }
}
EOF


./bin/tool-under-test install -o out2 --workspace-root src \
	-L '["env", "PATH='"${PATH}"'"]' \
	--local-build-root .root deep  'OK up' 2>&1
grep OK out2/out

# Upwards refernces and targets outside the repo are not OK

cat > TARGETS <<'EOF'
{ "outside.txt": {"type": "file_gen", "name": "inside.txt", "data": "BAD"}}
EOF

for REF in '"../outside.txt"' \
	       '["FILE", ".", "../outside.txt"]' \
	       '["./", "..", "outside.txt"]'
do echo "== true upwards reference $REF =="

cat > src/TARGETS <<EOF
{ "it":
  { "type": "generic"
  , "cmds": ["cat *.txt > out"]
  , "outs": ["out"]
  , "deps": [${REF}]
  }
}
EOF

cat src/TARGETS

./bin/tool-under-test analyse --workspace-root src . it 2>&1 && exit 1 || :

done


echo DONE
