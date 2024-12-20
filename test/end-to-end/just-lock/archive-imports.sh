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

readonly JUST_LOCK="${PWD}/bin/lock-tool-under-test"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly LOCK_LBR="${TEST_TMPDIR}/lock-local-build-root"
readonly OUT="${TEST_TMPDIR}/build-output"
readonly DISTDIR="${TEST_TMPDIR}/distfiles"
readonly REPO_DIRS="${TEST_TMPDIR}/repos"
readonly WRKDIR="${PWD}/work"

mkdir -p "${DISTDIR}"

# Repo foo
mkdir -p "${REPO_DIRS}/foo/root/src"
cd "${REPO_DIRS}/foo"
cat > root/repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": "src"}}}}
EOF
cat > root/src/TARGETS <<'EOF'
{ "": {"type": "file_gen", "name": "foo.txt", "data": "FOO"}}
EOF
tar cf "${DISTDIR}/foo-1.2.3.tar" . 2>&1
foocontent=$(git hash-object "${DISTDIR}/foo-1.2.3.tar")
echo "Foo archive has content ${foocontent}"

# Repo bar
mkdir -p "${REPO_DIRS}/bar"
cd "${REPO_DIRS}/bar"
cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": ""}}}}
EOF
cat > TARGETS <<'EOF'
{ "": {"type": "file_gen", "name": "bar.txt", "data": "BAR"}}
EOF
tar cf "${DISTDIR}/bar-1.2.3.tar" . 2>&1
barcontent=$(git hash-object "${DISTDIR}/bar-1.2.3.tar")
echo "Bar archive has content ${barcontent}"

# As we do not have an actual remote server to host the archives, make them
# available in local CAS instead
HASHFOO=$("${JUST}" add-to-cas --local-build-root "${LOCK_LBR}" "${DISTDIR}/foo-1.2.3.tar")
[ "${HASHFOO}" = "${foocontent}" ]
HASHBAR=$("${JUST}" add-to-cas --local-build-root "${LOCK_LBR}" "${DISTDIR}/bar-1.2.3.tar")
[ "${HASHBAR}" = "${barcontent}" ]

# Main repo
mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > TARGETS <<'EOF'
{ "":
  { "type": "generic"
  , "cmds": ["cat foo.txt bar.txt > out.txt"]
  , "outs": ["out.txt"]
  , "deps": [["@", "foo", "", ""], ["@", "bar", "", ""]]
  }
}
EOF

# Import repos as archives
cat > repos.in.json <<EOF
{ "repositories":
  { "":
    { "repository": {"type": "file", "path": "."}
    , "bindings": {"foo": "foo", "bar": "bar"}
    }
  }
  , "imports":
  [ { "source": "archive"
    , "repos": [{"alias": "foo"}]
    , "fetch": "http://non-existent.example.org/foo-1.2.3.tar"
    , "content": "${foocontent}"
    , "subdir": "root"
    }
  , { "source": "archive"
    , "repos": [{"alias": "bar"}]
    , "fetch": "http://non-existent.example.org/bar-1.2.3.tar"
    , "content": "${barcontent}"
    }
  ]
}
EOF
echo
cat repos.in.json

"${JUST_LOCK}" -C repos.in.json -o repos.json --local-build-root "${LOCK_LBR}" 2>&1
cat repos.json
echo
"${JUST_MR}" -L '["env", "PATH='"${PATH}"'"]' --norc --just "${JUST}" \
             --distdir "${DISTDIR}" --local-build-root "${LBR}" install -o "${OUT}" 2>&1
echo
cat "${OUT}/out.txt"
echo
grep -q FOOBAR "${OUT}/out.txt"

echo "OK"
