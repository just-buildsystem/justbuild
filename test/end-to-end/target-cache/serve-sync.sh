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
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR_LOCAL="${TEST_TMPDIR}/lbr.local"
readonly LBR="${TEST_TMPDIR}/lbr.with-serve"
readonly WRKDIR="${PWD}/work"
readonly LOCAL_SRC_DIR="${TEST_TMPDIR}/src"


if [ "${COMPATIBLE:-}" = "YES" ]; then
  ARGS="--compatible"
else
  ARGS=""
fi


RE_ARGS="${ARGS} -r ${REMOTE_EXECUTION_ADDRESS}"
SERVE_ARGS="${ARGS} -R ${SERVE} -r ${REMOTE_EXECUTION_ADDRESS}"

mkdir -p "${LOCAL_SRC_DIR}"
cd "${LOCAL_SRC_DIR}"
git init
git config user.name "Nobody"
git config user.email "nobody@example.org"

cat > TARGETS <<'EOF'
{ "local (unexported)":
  { "type": ["@", "rules", "CC", "library"]
  , "hdrs": ["bar.hpp"]
  , "deps": [["@", "lib", "", ""]]
  }
, "local": {"type": "export", "target": "local (unexported)"}
, "":
  { "type": ["@", "rules", "CC", "binary"]
  , "name": ["use"]
  , "srcs": ["use.cpp"]
  , "private-deps": ["local", ["@", "lib", "", ""]]
  }
}
EOF
cat > bar.hpp <<'EOF'
int bar(int x) { return x+1;}
EOF
cat > use.cpp <<'EOF'
#include "foo.hpp"
#include "bar.hpp"

#include <iostream>

int main(int arc, char **argv) {
  std::cout << "foo(2)=" << foo(2) << "\n";
  std::cout << "bar(2)=" << bar(2) << std::endl;
  return 0;
}
EOF

git add .
git commit -m 'Initial commit

... of a repository available locally but not to serve.'
readonly COMMIT=$(git log --pretty=%H)



mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT

# Set up repositories; everythig is content-fixed
# - "lib" is known to serve ahead of time
# - "rules" is "to_git", hence will be made known to the serve end point
# - "" is only known locally, not to the serve end point
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "git"
      , "repository": "${LOCAL_SRC_DIR}"
      , "branch": "master"
      , "commit": "${COMMIT}"
      }
    , "bindings": {"rules": "rules", "lib": "lib"}
    }
  , "rules":
    { "repository":
      {"type": "file", "path": "../src/rules", "pragma": {"to_git": true}}
    }
  , "lib":
    { "repository":
      {"type": "file", "path": "../samplelib", "pragma": {"to_git": true}}
    , "bindings": {"rules": "rules"}
    }
  }
}
EOF
cat repos.json

# Our target structure is that
# - lib is a library, with a generated hdr file
# - local is a target with a public dependency on lib, hence
#   forwarding the header files
# - the default target depends on both, hence the header file of lib and
#   its reexported version of local must not conflict

# Build all locally, demonstrating the extensional projection and
# that the export structure is correct.
"${JUST_MR}" --local-build-root "${LBR_LOCAL}" --just "${JUST}" \
  -L '["env", "PATH='"${PATH}"'"]'  build 2>&1
"${JUST_MR}" --local-build-root "${LBR_LOCAL}" --just "${JUST}" \
  -L '["env", "PATH='"${PATH}"'"]'  build 2>&1


echo
echo 'Serve build'
echo
# Now, with a completely fresh local build root, build the default target, using
# a serve endpoint which can provide ["@", "lib", "", ""],
# but not  ["@", "", "", "local"].
"${JUST_MR}" --local-build-root "${LBR}" --just "${JUST}" ${SERVE_ARGS} \
  -L '["env", "PATH='"${PATH}"'"]'  build 2>&1

echo 'remote build (same endpoint)'
echo
# Now, when continuing without serve (but still using remote-exection), things
# should still be in a consistent state, without causing staging conflicts.
echo
"${JUST_MR}" --local-build-root "${LBR}" --just "${JUST}" ${RE_ARGS} \
  -L '["env", "PATH='"${PATH}"'"]'  build 2>&1


echo OK
