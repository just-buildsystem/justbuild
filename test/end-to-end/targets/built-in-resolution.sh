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

TOOL=$(realpath ./bin/tool-under-test)
mkdir -p .root
BUILDROOT=$(realpath .root)
mkdir -p out
OUTDIR=$(realpath out)

mkdir -p  src/some/module
touch src/ROOT
cd src/some/module
cat > RULES <<'EOI'
{ "install":
  { "expression":
    { "type": "RESULT"
    , "artifacts":
      { "type": "singleton_map"
      , "key": "some internal path"
      , "value": {"type": "BLOB", "data": "FROM USER-DEFINED INSTALL"}
      }
    }
  }
}
EOI
cat > TARGETS <<'EOI'
{ "user": {"type": ["./", ".", "install"]}
, "built-in": {"type": "install", "files": {"the/public/path": "user"}}
}
EOI

echo
${TOOL} install built-in --local-build-root ${BUILDROOT} -o ${OUTDIR} 2>&1
echo
grep 'FROM USER-DEFINED INSTALL' ${OUTDIR}/the/public/path
echo

echo DONE
