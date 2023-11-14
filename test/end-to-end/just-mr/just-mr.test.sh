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

set -eu

# cleanup of http.server; pass server_pid as arg
server_cleanup() {
  echo "Shut down HTTP server"
  # send SIGTERM
  kill ${1} & res=$!
  wait ${res}
  echo "done"
}

readonly ROOT=`pwd`
readonly WRKDIR="${TEST_TMPDIR}/wrkdir"

readonly JUST_MR_CPP="${ROOT}/bin/mr-tool-under-test"

# set paths
readonly SERVER_ROOT="${TEST_TMPDIR}/server-root"
readonly FILE_ROOT="${TEST_TMPDIR}/file-root"
readonly GIT_ROOT="${TEST_TMPDIR}/git-root"
readonly TEST_ROOT="${TEST_TMPDIR}/test-root"

# move to where server will be posted
mkdir -p "${SERVER_ROOT}"
cd "${SERVER_ROOT}"

echo "Setup archive repos"
# create the archives in current dir
${ROOT}/src/create_test_archives

# store zip repo info
readonly ZIP_REPO_CONTENT=$(git hash-object zip_repo.zip)
readonly ZIP_REPO_SHA256=$(sha256sum zip_repo.zip | awk '{print $1}')
readonly ZIP_REPO_SHA512=$(sha512sum zip_repo.zip | awk '{print $1}')
# store tar.gz repo info
readonly TGZ_REPO_CONTENT=$(git hash-object tgz_repo.tar.gz)
readonly TGZ_REPO_SHA256=$(sha256sum tgz_repo.tar.gz | awk '{print $1}')
readonly TGZ_REPO_SHA512=$(sha512sum tgz_repo.tar.gz | awk '{print $1}')

echo "Set up file repos"
# add files
mkdir -p "${FILE_ROOT}/test_dir1"
echo test > "${FILE_ROOT}/test_dir1/test_file"
mkdir -p "${FILE_ROOT}/test_dir2/test_dir3"
echo test > "${FILE_ROOT}/test_dir2/test_dir3/test_file"
# add resolvable non-upwards symlink
ln -s test_file "${FILE_ROOT}/test_dir1/nonupwards"
# add resolvable upwards symlink
ln -s ../test_dir3/test_file "${FILE_ROOT}/test_dir2/test_dir3/upwards"

echo "Set up local git repo"
# NOTE: Libgit2 has no support for Git bundles yet
mkdir -p "${GIT_ROOT}"
mkdir -p bin
cat > bin/git_dir_setup.sh <<EOF
mkdir -p foo/bar
echo foo > foo.txt
echo bar > foo/bar.txt
echo baz > foo/bar/baz.txt
ln -s dummy foo/link
EOF
# set up git repo
(
  cd "${GIT_ROOT}" \
  && sh "${SERVER_ROOT}/bin/git_dir_setup.sh" \
  && git init \
  && git checkout -q -b test \
  && git config user.email "nobody@example.org" \
  && git config user.name "Nobody" \
  && git add . \
  && git commit -m "test" --date="1970-01-01T00:00Z"
)
readonly GIT_REPO_COMMIT="$(cd "${GIT_ROOT}" && git rev-parse HEAD)"

echo "Set up git tree repo"
# get tree id; only the content of the directory structure matters
readonly GIT_TREE_ID="$(cd "${GIT_ROOT}" && git ls-tree "${GIT_REPO_COMMIT}" foo/bar | awk '{print $3}')"

echo "Publish remote repos to HTTP server"
# start Python server as remote repos location
port_file="$(mktemp -t port_XXXXXX -p "${TEST_TMPDIR}")"
python3 -u "${ROOT}/utils/run_test_server.py" "${port_file}" & server_pid=$!
# set up cleanup of http server
trap "server_cleanup ${server_pid}" INT TERM EXIT
# wait for the server to be available
tries=0
while [ -z "$(cat "${port_file}")" ] && [ $tries -lt 10 ]
do
    tries=$((${tries}+1))
    sleep 1s
done
# get port number as variable
port_num="$(cat ${port_file})"
if [ -z "${port_num}" ]; then
    exit 1
fi

echo "Create local build env"
# the build root
readonly BUILDROOT=${WRKDIR}/.cache/just
mkdir -p ${BUILDROOT}
# a distdir
readonly DISTFILES=${WRKDIR}/.distfiles
mkdir -p ${DISTFILES}
readonly DISTDIR_ARGS="--distdir ${DISTFILES}"

echo "Create repos.json"
mkdir -p "${TEST_ROOT}"
cd "${TEST_ROOT}"
cat > test-repos.json <<EOF
{ "repositories":
  { "zip_repo":
    { "repository":
      { "type": "zip"
      , "content": "${ZIP_REPO_CONTENT}"
      , "distfile": "zip_repo.zip"
      , "fetch": "http://127.0.0.1:${port_num}/zip_repo.zip"
      , "sha256": "${ZIP_REPO_SHA256}"
      , "sha512": "${ZIP_REPO_SHA512}"
      , "subdir": "root"
      , "pragma": {"special": "resolve-partially"}
      }
    , "bindings": {"tgz_repo": "tgz_repo", "distdir_repo": "distdir_repo"}
    }
  , "zip_repo_resolved":
    { "repository":
      { "type": "zip"
      , "content": "${ZIP_REPO_CONTENT}"
      , "distfile": "zip_repo.zip"
      , "fetch": "http://127.0.0.1:${port_num}/zip_repo.zip"
      , "sha256": "${ZIP_REPO_SHA256}"
      , "sha512": "${ZIP_REPO_SHA512}"
      , "subdir": "root"
      , "pragma": {"special": "resolve-completely"}
      }
    , "bindings": {"tgz_repo": "tgz_repo", "distdir_repo": "distdir_repo"}
    }
  , "tgz_repo":
    { "repository":
      { "type": "archive"
      , "content": "${TGZ_REPO_CONTENT}"
      , "distfile": "tgz_repo.tar.gz"
      , "fetch": "http://127.0.0.1:${port_num}/tgz_repo.tar.gz"
      , "sha256": "${TGZ_REPO_SHA256}"
      , "sha512": "${TGZ_REPO_SHA512}"
      , "subdir": "root/baz"
      , "pragma": {"special": "ignore"}
      }
    , "bindings": {"git_repo": "git_repo"}
    }
  , "git_repo":
    { "repository":
      { "type": "git"
      , "repository": "${GIT_ROOT}"
      , "branch": "test"
      , "commit": "${GIT_REPO_COMMIT}"
      , "subdir": "foo"
      }
    }
  , "git_repo_ignore_special":
    { "repository":
      { "type": "git"
      , "repository": "${GIT_ROOT}"
      , "branch": "test"
      , "commit": "${GIT_REPO_COMMIT}"
      , "subdir": "foo"
      , "pragma": {"special": "ignore"}
      }
    }
  , "git_tree_repo":
    { "repository":
      { "type": "git tree"
      , "id": "${GIT_TREE_ID}"
      , "cmd": ["sh", "${SERVER_ROOT}/bin/git_dir_setup.sh"]
      }
    }
  , "git_tree_repo_ignore_special":
    { "repository":
      { "type": "git tree"
      , "id": "${GIT_TREE_ID}"
      , "cmd": ["sh", "${SERVER_ROOT}/bin/git_dir_setup.sh"]
      , "pragma": {"special": "ignore"}
      }
    }
  , "file_repo1":
    { "repository":
      { "type": "file"
      , "path": "${FILE_ROOT}/test_dir1"
      , "pragma": {"to_git": true}
      }
    }
  , "file_repo2_ignore_special":
    { "repository":
      { "type": "file"
      , "path": "${FILE_ROOT}/test_dir2"
      , "pragma": {"special": "ignore"}
      }
    }
  , "file_repo2_resolve_partially":
    { "repository":
      { "type": "file"
      , "path": "${FILE_ROOT}/test_dir2"
      , "pragma": {"special": "resolve_partially"}
      }
    }
  , "file_repo2_resolve_completely":
    { "repository":
      { "type": "file"
      , "path": "${FILE_ROOT}/test_dir2"
      , "pragma": {"special": "resolve_completely"}
      }
    }
  , "distdir_repo":
    { "repository":
      { "type": "distdir"
      , "repositories": ["git_repo", "zip_repo", "file_repo1"]
      }
    , "bindings":
      { "file_repo2_ignore_special": "file_repo2_ignore_special"
      , "file_repo2_resolve_partially": "file_repo2_resolve_partially"
      , "file_repo2_resolve_completely": "file_repo2_resolve_completely"
      }
    }
  }
}
EOF

echo "Set up test cases"
cat > test_setup.sh <<EOF
CONFIG_CPP="\$(${JUST_MR_CPP} -C test-repos.json --norc --local-build-root ${BUILDROOT} ${DISTDIR_ARGS} -j 32 setup --all)"
if [ ! -s "\${CONFIG_CPP}" ]; then
    exit 1
fi
EOF

echo "Run 8 test cases in parallel"
error=false

sh test_setup.sh & res1=$!
sh test_setup.sh & res2=$!
sh test_setup.sh & res3=$!
sh test_setup.sh & res4=$!

wait ${res1}
if [ $? -ne 0 ]; then
    error=true
fi
wait ${res2}
if [ $? -ne 0 ]; then
    error=true
fi
wait ${res3}
if [ $? -ne 0 ]; then
    error=true
fi
wait ${res4}
if [ $? -ne 0 ]; then
    error=true
fi

# check test status
if [ $error = true ]; then
    exit 1
fi
