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

env

readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/out"
readonly LOG="${TEST_TMPDIR}/log"

COMPAT=""
if [ "${COMPATIBLE:-}" = "YES" ]; then
  COMPAT="--compatible"
fi

ARCHIVE_CONTENT=$(git hash-object src/data.tar)
echo "Archive has content $ARCHIVE_CONTENT"

mkdir work
cd work
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "archive":
    { "repository":
      { "type": "archive"
      , "content": "${ARCHIVE_CONTENT}"
      , "fetch": "http://non-existent.example.org/data.tar"
      }
    }
  , "targets": {"repository": {"type": "file", "path": "targets"}}
  , "":
    { "repository": {"type": "distdir", "repositories": ["archive"]}
    , "target_root": "targets"
    }
  }
}
EOF
mkdir targets
cat > targets/TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["archive_id"]
  , "cmds": ["git hash-object data.tar > archive_id"]
  , "deps": ["data.tar"]
  }
}
EOF

echo
cat repos.json
echo
mkdir -p "${LOG}"
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" \
                    -r "${REMOTE_EXECUTION_ADDRESS}" ${COMPAT} \
                    -f "${LOG}/log" \
                    --distdir ../src \
                    install -o "${OUT}" 2>&1
echo
cat "${OUT}/archive_id"
[ $(cat "${OUT}/archive_id") = "${ARCHIVE_CONTENT}" ]
echo
# As the build succeeded, there should not be any error reported
cat "${LOG}/log"
echo
grep ERROR "${LOG}/log" && exit 1 || :

echo
echo OK
