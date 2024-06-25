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

ROOT="$(pwd)"
JUST="${ROOT}/bin/tool-under-test"
LBR="${TEST_TMPDIR}/build-root"
OUT="${TEST_TMPDIR}/output"

mkdir work
cd work
touch ROOT

cat > TARGETS <<'EOF'
{ "payload":
  { "type": "file_gen"
  , "arguments_config": ["NAME"]
  , "name": "greeting.txt"
  , "data":
    {"type": "join", "$1": ["Hello ", {"type": "var", "name": "NAME"}, "!"]}
  }
, "exported-target":
  { "type": "export"
  , "doc": ["Here we export the canonical GREETING text"]
  , "target": "payload"
  , "flexible_config": ["NAME"]
  , "config_doc": {"NAME": ["The RECIPIENT of the greeting"]}
  }
, "":
  { "type": "configure"
  , "doc": ["Configure the canonical greeting to set a DEFAULT name"]
  , "arguments_config": ["NAME"]
  , "target": "exported-target"
  , "config":
    { "type": "singleton_map"
    , "key": "NAME"
    , "value": {"type": "var", "name": "NAME", "default": "world"}
    }
  }
}
EOF

# Sanity check: the target description actually works
"${JUST}" install --local-build-root "${LBR}" -o "${OUT}" 2>&1
grep 'Hello world' "${OUT}/greeting.txt"

# Verify the description of the export target
"${JUST}" describe --local-build-root "${LBR}" exported-target \
          > "${OUT}/export.doc" 2> "${OUT}/log"
echo
cat "${OUT}/log"
echo
cat "${OUT}/export.doc"
echo
echo
# - rule name
grep '"export"' "${OUT}/export.doc"
# - top-level description
grep GREETING "${OUT}/export.doc"
# - flexible config
grep NAME "${OUT}/export.doc"
# - documentation of variable
grep RECIPIENT "${OUT}/export.doc"

# Verify the description of the configure target
"${JUST}" describe --local-build-root "${LBR}" \
          > "${OUT}/configure.doc" 2> "${OUT}/log"
echo
cat "${OUT}/log"
echo
cat "${OUT}/configure.doc"
echo
echo
# - rule name
grep '"configure"' "${OUT}/configure.doc"
# - top-level description
grep DEFAULT "${OUT}/configure.doc"
# - the expression defining what to configure
grep '"exported-target"' "${OUT}/configure.doc"

echo OK
