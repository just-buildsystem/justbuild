#!/bin/sh
# Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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
readonly LBR_NON_ABSENT="${TEST_TMPDIR}/local-build-root-non-absent"
readonly LBR_FOR_FETCH="${TEST_TMPDIR}/local-build-root-for-fetch"
readonly OUT="${TEST_TMPDIR}/out"
readonly OUT2="${TEST_TMPDIR}/out2"
readonly OUT3="${TEST_TMPDIR}/out3"
readonly OUT_NON_ABSENT="${TEST_TMPDIR}/out4"
readonly OUT_DISTDIR="${TEST_TMPDIR}/out4"

ARCHIVE_CONTENT=$(git hash-object src/data.tar)
echo "Archive has content $ARCHIVE_CONTENT"

mkdir work
cd work
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "archive"
      , "content": "${ARCHIVE_CONTENT}"
      , "pragma": {"absent": true}
      , "fetch": "http://non-existent.example.org/data.tar"
      , "subdir": "src"
      }
    , "target_root": "targets"
    }
  , "targets": {"repository": {"type": "file", "path": "targets"}}
  }
}
EOF
mkdir targets
cat > targets/TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["head -c 1 4.txt > out.txt", "cat 2.txt >> out.txt"]
  , "deps": ["4.txt", "2.txt"]
  }
}
EOF

echo
cat repos.json
echo
CONF=$("${JUST_MR}" --norc --local-build-root "${LBR}" \
                    -L '["env", "PATH='"${PATH}"'"]' \
                    --remote-serve-address ${SERVE} \
                    -r ${REMOTE_EXECUTION_ADDRESS} \
                    --fetch-absent setup)
cat $CONF
echo
"${JUST}" install --local-build-root "${LBR}" -C "${CONF}" \
          -L '["env", "PATH='"${PATH}"'"]' \
          -r "${REMOTE_EXECUTION_ADDRESS}" -o "${OUT}" 2>&1
grep 42 "${OUT}/out.txt"

# As the last call of just-mr had --fetch-absent, all relevent information
# about the root should now be available locally, so we can build without
# a serve or remote endpoint with still (logically) fetching absent roots.
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" \
             -L '["env", "PATH='"${PATH}"'"]' \
             --fetch-absent install -o "${OUT2}" 2>&1
grep 42 "${OUT2}/out.txt"

# Now take the same repo, but without the subdir, to ensure we did not
# cache a wrong association.
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "archive"
      , "content": "${ARCHIVE_CONTENT}"
      , "pragma": {"absent": true}
      , "fetch": "http://non-existent.example.org/data.tar"
      }
    , "target_root": "targets"
    }
  , "targets": {"repository": {"type": "file", "path": "targets"}}
  }
}
EOF
cat > targets/TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["head -c 1 src/4.txt > out.txt", "cat src/2.txt >> out.txt"]
  , "deps": ["src/4.txt", "src/2.txt"]
  }
}
EOF
"${JUST_MR}" --norc --local-build-root "${LBR}" \
             -L '["env", "PATH='"${PATH}"'"]' \
             --remote-serve-address ${SERVE} \
             -r ${REMOTE_EXECUTION_ADDRESS} \
             --just "${JUST}" \
             --fetch-absent install -o "${OUT3}" 2>&1
grep 42 "${OUT3}/out.txt"

# Now, on a fresh local build root, take the original description
# without any absent hint. In this way, we verify that even for
# concrete repositories fetching over the just-serve instance works
# (as we removed all other ways to get the archive).
cat > repos.json <<EOF
{ "repositories":
  { "":
    { "repository":
      { "type": "archive"
      , "content": "${ARCHIVE_CONTENT}"
      , "fetch": "http://non-existent.example.org/data.tar"
      , "subdir": "src"
      }
    , "target_root": "targets"
    }
  , "targets": {"repository": {"type": "file", "path": "targets"}}
  }
}
EOF
cat > targets/TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["head -c 1 4.txt > out.txt", "cat 2.txt >> out.txt"]
  , "deps": ["4.txt", "2.txt"]
  }
}
EOF
echo
cat repos.json
echo
"${JUST_MR}" --norc --local-build-root "${LBR_NON_ABSENT}" \
             -L '["env", "PATH='"${PATH}"'"]' \
             --remote-serve-address ${SERVE} \
             -r ${REMOTE_EXECUTION_ADDRESS} \
             --just "${JUST}" \
             install -o "${OUT_NON_ABSENT}" 2>&1
grep 42 "${OUT_NON_ABSENT}/out.txt"


## Finally, verify that the archive itself can also be fetched
echo
mkdir -p "${OUT_DISTDIR}"
"${JUST_MR}" --norc --local-build-root "${LBR_FOR_FETCH}" \
             --remote-serve-address ${SERVE} \
             -r ${REMOTE_EXECUTION_ADDRESS} \
             fetch -o "${OUT_DISTDIR}" 2>&1
FETCHED_CONTENT=$(git hash-object "${OUT_DISTDIR}"/data.tar)
echo
echo Fetched content ${FETCHED_CONTENT}
[ "${ARCHIVE_CONTENT}" = "${FETCHED_CONTENT}" ]

echo DONE
