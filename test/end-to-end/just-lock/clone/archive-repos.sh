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

readonly JUST_LOCK="${PWD}/bin/lock-tool-under-test"
readonly JUST="${PWD}/bin/tool-under-test"
readonly JUST_MR="${PWD}/bin/mr-tool-under-test"

readonly LOCK_LBR="${TEST_TMPDIR}/local-build-root"
readonly LBR_ARCHIVES="${TEST_TMPDIR}/local-build-root-archives"
readonly LBR_CLONES="${TEST_TMPDIR}/local-build-root-clones"

readonly INSTALL_ZIP="${TEST_TMPDIR}/install-zip"
readonly INSTALL_TGZ="${TEST_TMPDIR}/install-tgz"
readonly INSTALL_FOREIGN="${TEST_TMPDIR}/install-foreign"
readonly INSTALL_DISTDIR="${TEST_TMPDIR}/install-distdir"

readonly ROOT=`pwd`
readonly DISTDIR="${TEST_TMPDIR}/distfiles"
readonly WRKDIR="${TEST_TMPDIR}/work"

# Set up zip and tgz archives
mkdir -p "${DISTDIR}"
cd "${DISTDIR}"

${ROOT}/src/create_test_archives

readonly ZIP_REPO_CONTENT=$("${JUST}" add-to-cas --local-build-root "${LOCK_LBR}" "${DISTDIR}/zip_repo.zip")
readonly TGZ_REPO_CONTENT=$("${JUST}" add-to-cas --local-build-root "${LOCK_LBR}" "${DISTDIR}/tgz_repo.tar.gz")

# Set up foreign file
echo Here be dragons > "${DISTDIR}/foreign.txt"
readonly FOREIGN=$("${JUST}" add-to-cas --local-build-root "${LOCK_LBR}" "${DISTDIR}/foreign.txt")

# Input configuration file
mkdir -p "${WRKDIR}"
cd "${WRKDIR}"
touch ROOT
cat > repos.in.json <<EOF
{ "repositories":
  { "zip_repo":
    { "repository":
      { "type": "zip"
      , "content": "${ZIP_REPO_CONTENT}"
      , "fetch": "http://non-existent.example.org/zip_repo.zip"
      , "subdir": "root"
      , "pragma": {"special": "resolve-partially"}
      }
    }
  , "tgz_repo":
    { "repository":
      { "type": "archive"
      , "content": "${TGZ_REPO_CONTENT}"
      , "fetch": "http://non-existent.example.org/tgz_repo.tar.gz"
      , "subdir": "root/baz"
      , "pragma": {"special": "ignore"}
      }
    }
  , "foreign_file":
    { "repository":
      { "type": "foreign file"
      , "content": "${FOREIGN}"
      , "fetch": "http://nonexistent.example.com/foreign.txt"
      , "name": "data.txt"
      }
    }
  , "distdir_repo":
    { "repository":
      { "type": "distdir"
      , "repositories": ["zip_repo", "tgz_repo"]
      }
    }
  }
}
EOF
echo
echo Input config:
cat repos.in.json
echo

# Check setup with archived content
CONF=$("${JUST_MR}" -L '["env", "PATH='"${PATH}"'"]' --norc --just "${JUST}" \
                      -C repos.in.json --distdir "${DISTDIR}" \
                      --local-build-root "${LBR_ARCHIVES}" setup --all) 2>&1
echo

echo Clone repos:
CLONE_TO="cloned_foo"
cat > clone.json <<EOF
{ "${CLONE_TO}/zip": ["zip_repo", []]
, "${CLONE_TO}/tgz": ["tgz_repo", []]
, "${CLONE_TO}/foreign": ["foreign_file", []]
, "${CLONE_TO}/distdir": ["distdir_repo", []]
}
EOF
"${JUST_LOCK}" -C repos.in.json -o repos.json --local-build-root "${LOCK_LBR}" \
               --just "${JUST}" --clone $(cat clone.json | jq -c) 2>&1
echo
echo Output config:
cat repos.json
echo

echo Check output configuration:
grep "${CLONE_TO}/zip" repos.json
grep "${CLONE_TO}/tgz" repos.json
grep "${CLONE_TO}/foreign" repos.json
grep "${CLONE_TO}/distdir" repos.json
[ ! $(grep pragma repos.json) ]  # special pragmas should have been resolved
echo

# Check setup with local clones:
"${JUST_MR}" -L '["env", "PATH='"${PATH}"'"]' --norc --just "${JUST}" \
             -C repos.json --local-build-root "${LBR_CLONES}" setup --all 2>&1
echo

# Check that the clones have the expected content
"${JUST}" install-cas --local-build-root "${LBR_ARCHIVES}" \
    "$(jq '."repositories"."zip_repo"."workspace_root" | .[1]' "${CONF}" | tr -d '"')::t" \
    -P root -o "${INSTALL_ZIP}"
diff -ruN "${INSTALL_ZIP}" "${CLONE_TO}/zip"

"${JUST}" install-cas --local-build-root "${LBR_ARCHIVES}" \
    "$(jq '."repositories"."tgz_repo"."workspace_root" | .[1]' "${CONF}" | tr -d '"')::t" \
    -P root/baz -o "${INSTALL_TGZ}"
diff -ruN "${INSTALL_TGZ}" "${CLONE_TO}/tgz"

"${JUST}" install-cas --local-build-root "${LBR_ARCHIVES}" \
    "$(jq '."repositories"."foreign_file"."workspace_root" | .[1]' "${CONF}" | tr -d '"')::t" \
    -o "${INSTALL_FOREIGN}"
diff -ruN "${INSTALL_FOREIGN}" "${CLONE_TO}/foreign"

"${JUST}" install-cas --local-build-root "${LBR_ARCHIVES}" \
    "$(jq '."repositories"."distdir_repo"."workspace_root" | .[1]' "${CONF}" | tr -d '"')::t" \
    -o "${INSTALL_DISTDIR}"
diff -ruN "${INSTALL_DISTDIR}" "${CLONE_TO}/distdir"

echo "OK"
