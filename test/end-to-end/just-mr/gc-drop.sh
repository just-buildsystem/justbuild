#!/bin/sh
# Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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
readonly WRKDIR="${PWD}/work"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/out"
readonly ORIG_SRC="${TEST_TMPDIR}/orig"




# Generate an archive and make the content known to CAS
# =====================================================

mkdir -p "${ORIG_SRC}"
cd "${ORIG_SRC}"
mkdir -p src/some/deep/directory
echo 'some data values' > src/some/deep/directory/data.txt
cat > src/TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "deps": ["some/deep/directory/data.txt"]
  , "cmds": ["cat some/deep/directory/data.txt | tr a-z A-Z > out.txt"]
  }
}
EOF
tar cf source.tar src
SOURCE_HASH_A=$("${JUST}" add-to-cas --local-build-root "${LBR}" source.tar)
rm -rf source.tar src

echo "Upstream hash A is ${SOURCE_HASH_A}"

# Generate a second, larger, archive and make the content known to CAS
# ====================================================================

mkdir -p "${ORIG_SRC}"
cd "${ORIG_SRC}"
rm -rf src source.tar

mkdir -p src
for i in `seq 1 100`
do
    echo "content of file ${i}" > src/data-$i.txt
done
cat > src/TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "deps": [["GLOB", null, "data*.txt"]]
  , "cmds": ["cat data-*.txt | sort > out.txt"]
  }
}
EOF
tar cf source.tar src
SOURCE_HASH_B=$("${JUST}" add-to-cas --local-build-root "${LBR}" source.tar)
rm -rf source.tar src

echo "Upstream hash B is ${SOURCE_HASH_B}"


# Use both archives
# =================

mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "a":
    { "repository":
      { "type": "archive"
      , "content": "${SOURCE_HASH_A}"
      , "fetch": "http://nonexistent.upstream.example.com/source.tar"
      , "subdir": "src"
      }
    }
  , "b":
    { "repository":
      { "type": "archive"
      , "content": "${SOURCE_HASH_B}"
      , "fetch": "http://nonexistent.upstream.example.com/source.tar"
      , "subdir": "src"
      }
    }
  }
}
EOF
cat repos.json

# archive a
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" --main a \
          -L '["env", "PATH='"${PATH}"'"]' install -o "${OUT}" 2>&1
# ... sanity check
grep VALUES "${OUT}/out.txt"

# archive b
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" --main b \
          -L '["env", "PATH='"${PATH}"'"]' install -o "${OUT}" 2>&1
# ... sanity check
grep 42 "${OUT}/out.txt"

# Rotate, and use a again
# =======================

"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" gc 2>&1
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" gc 2>&1
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" gc-repo 2>&1

"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" --main a \
          -L '["env", "PATH='"${PATH}"'"]' install -o "${OUT}" 2>&1
grep VALUES "${OUT}/out.txt"


# Verify gc-repo --drop
# =====================

# We have the situation that root a is in the young generation, whereas
# b is only in the old generation. Therefore, gc-repo --drop-only should
# clean up b, which should result in reduced disk usage.

# get rid of CAS/cahe
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" gc 2>&1
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" gc 2>&1

# measure disk usage
PRE_DROP_DISK=$(du -sb "${LBR}" | cut -f 1)
echo "Pre drop, disk usage is ${PRE_DROP_DISK}"

# drop
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" gc-repo --drop-only 2>&1


# measure disk usage
POST_DROP_DISK=$(du -sb "${LBR}" | cut -f 1)
echo "Post drop, disk usage is ${POST_DROP_DISK}"

# post drop the disk usage should be strictly less
[ "${POST_DROP_DISK}" -lt "${PRE_DROP_DISK}" ]

# Verify that a is still in the youngest generation: even after one more
# rotation, we should be able to build a.
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" gc 2>&1
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" gc 2>&1
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" gc-repo 2>&1
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" --main a \
          -L '["env", "PATH='"${PATH}"'"]' install -o "${OUT}" 2>&1
grep VALUES "${OUT}/out.txt"

# Finally demonstrate that the root was not taken from anything but the cache
# ===========================================================================

"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" gc 2>&1
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" gc 2>&1
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" gc-repo 2>&1
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${JUST}" gc-repo 2>&1
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" --main a \
          -L '["env", "PATH='"${PATH}"'"]' build 2>&1 && exit 1 || :

echo OK
