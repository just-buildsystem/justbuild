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

###
# Test that users are warned if a non-content-fixed repository is reached from
# an absent main repository.
##

set -eu

readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly FILE_ROOT="${TEST_TMPDIR}/file-repos"
readonly TEST_ROOT="${TEST_TMPDIR}/test-root"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly OUT="${TEST_TMPDIR}/out"
mkdir -p "${OUT}"

# Set up file repos
mkdir -p "${FILE_ROOT}/test_dir1"
echo test > "${FILE_ROOT}/test_dir1/test_file"
mkdir -p "${FILE_ROOT}/test_dir2/inner_dir"
echo test > "${FILE_ROOT}/test_dir2/inner_dir/test_file"
mkdir -p "${FILE_ROOT}/test_dir3/inner_dir"
echo test > "${FILE_ROOT}/test_dir3/inner_dir/test_file"

# Setup sample repository config
mkdir -p "${TEST_ROOT}"
cd "${TEST_ROOT}"
touch ROOT
cat > repos.json <<EOF
{ "repositories":
  { "main":
    { "repository":
      { "type": "file"
      , "path": "."
      , "pragma": {"absent": true}
      }
    , "bindings":
      { "dep1": "file_repo_not_content_fixed"
      , "dep2": "file_repo_to_git_explicit"
      , "dep3": "file_repo_to_git_implicit"
      }
    }
  , "file_repo_not_content_fixed":
    { "repository":
      { "type": "file"
      , "path": "${FILE_ROOT}/test_dir1"
      }
    }
  , "file_repo_to_git_explicit":
    { "repository":
      { "type": "file"
      , "path": "${FILE_ROOT}/test_dir2"
      , "pragma": {"to_git": true}
      }
    }
  , "file_repo_to_git_implicit":
    { "repository":
      { "type": "file"
      , "path": "${FILE_ROOT}/test_dir3"
      , "pragma": {"special": "resolve-partially"}
      }
    }
  }
}
EOF
echo "Repository configuration:"
cat repos.json

echo
echo Run setup of absent main repository
echo
"${JUST_MR}" --norc --local-build-root "${LBR}" -f "${OUT}/log.txt" setup main 2>&1

echo
echo Check expected warnings
echo

grep 'WARN.* "main" .*absent.* "main"' "${OUT}/log.txt"
grep 'WARN.* "file_repo_not_content_fixed" .*absent.* "main"' "${OUT}/log.txt"
grep 'WARN.* "file_repo_to_git_explicit" .*absent.* "main"' "${OUT}/log.txt" &&
    echo "should have failed" && exit 1 || :
grep 'WARN.* "file_repo_to_git_implicit" .*absent.* "main"' "${OUT}/log.txt" &&
    echo "should have failed" && exit 1 || :

echo
echo OK
