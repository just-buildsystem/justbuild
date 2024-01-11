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
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly LBR_MR="${TEST_TMPDIR}/local-build-root-mr"
readonly TOOLS_DIR="${TEST_TMPDIR}/tools"
readonly OUT="${TEST_TMPDIR}/out"
readonly JUST_MR_ARGS="--norc --just ${JUST} --local-build-root ${LBR_MR}"
JUST_ARGS="--local-build-root ${LBR}"
BUILD_ARGS="${JUST_ARGS} --log-limit 4"
if [ -n "${COMPATIBLE:-}" ]; then
  BUILD_ARGS="$BUILD_ARGS --compatible"
fi


mkdir work
cd work
touch ROOT

# general setup: 2 export targets ["@", "foo", "", ""] and ["@", "bar", "", ""]
# with a common dependency, the export target ["@", "common", "", ""]

cat > repos.json <<'EOF'
{ "repositories":
  { "":
    { "repository":
      {"type": "file", "path": "top-level", "pragma": {"to_git": true}}
    , "bindings": {"foo": "foo", "bar": "bar", "common": "common"}
    }
  , "foo":
    { "repository": {"type": "file", "path": "foo", "pragma": {"to_git": true}}
    , "bindings": {"common": "common"}
    }
  , "bar":
    { "repository": {"type": "file", "path": "bar", "pragma": {"to_git": true}}
    , "bindings": {"common": "common"}
    }
  , "common":
    { "repository":
      {"type": "file", "path": "common", "pragma": {"to_git": true}}
    }
  }
}
EOF

mkdir common
cat > common/TARGETS <<'EOF'
{ "common.txt":
  { "type": "generic"
  , "outs": ["common.txt"]
  , "cmds": ["echo common > common.txt"]
  }
, "common": {"type": "install", "files": {"common.txt": "common.txt"}}
, "": {"type": "export", "target": "common"}
}
EOF

mkdir foo
cat > foo/TARGETS <<'EOF'
{ "foo.txt":
  {"type": "generic", "outs": ["foo.txt"], "cmds": ["echo foo > foo.txt"]}
, "foo":
  { "type": "install"
  , "files": {"foo.txt": "foo.txt", "common.txt": ["@", "common", "", ""]}
  }
, "": {"type": "export", "target": "foo"}
}
EOF

mkdir bar
cat > bar/TARGETS <<'EOF'
{ "bar.txt":
  {"type": "generic", "outs": ["bar.txt"], "cmds": ["echo bar > bar.txt"]}
, "bar":
  { "type": "install"
  , "files": {"bar.txt": "bar.txt", "common.txt": ["@", "common", "", ""]}
  }
, "": {"type": "export", "target": "bar"}
}
EOF

mkdir top-level
cat > top-level/TARGETS <<'EOF'
{"":
  { "type": "install"
  , "deps": [["@", "foo", "", ""], ["@", "bar", "", ""]]
  }
}
EOF

# Test the interaction with gc

echo
echo 'First build, gets foo, bar, common into tc'
echo
"${JUST_MR}" ${JUST_MR_ARGS} build ${BUILD_ARGS} 2>&1

echo
echo gc to put all into the old generation
echo
"${JUST}" gc ${JUST_ARGS} 2>&1

echo
echo 'build again; this gets foo and bar in the young generation'
echo
"${JUST_MR}" ${JUST_MR_ARGS} build ${BUILD_ARGS} 2>&1

echo
echo 'gc again; this would (without invariants) get common out'
echo
"${JUST}" gc ${JUST_ARGS} 2>&1

echo
echo 'Modify the root of bar'
echo

touch bar/bar_root_has_changed

echo
echo 'Analyse the relevant targets'
echo
"${JUST_MR}" ${JUST_MR_ARGS} --main foo analyse ${BUILD_ARGS} 2>&1
"${JUST_MR}" ${JUST_MR_ARGS} --main bar analyse ${BUILD_ARGS} 2>&1

echo
echo 'build again; this checks for staging conflics'
echo
"${JUST_MR}" ${JUST_MR_ARGS} build ${BUILD_ARGS} 2>&1

echo
echo OK
