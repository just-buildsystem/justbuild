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

# cleanup of http.server; pass server_pid as arg
server_cleanup() {
  echo "Shut down HTTP server(s)"
  while [ -n "${1:-}" ]; do
    # send SIGTERM
    kill ${1} & res=$!
    wait ${res}
    echo "done ${1}"
    shift
  done
}

readonly ROOT=`pwd`
readonly WRKDIR="${TEST_TMPDIR}/wrkdir"

readonly JUST_MR_CPP="${ROOT}/bin/mr-tool-under-test"

# set paths
readonly SERVER_ROOT="${TEST_TMPDIR}/server-root"
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

### Using "mirrors" config field

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
      , "fetch": "http://non-existent.example.org/zip_repo.zip"
      , "mirrors":
        [ "http://non-existent.example.org/zip_repo.zip"
        , "http://127.0.0.1:${port_num}/zip_repo.zip"
        ]
      , "sha256": "${ZIP_REPO_SHA256}"
      , "sha512": "${ZIP_REPO_SHA512}"
      , "subdir": "root"
      , "pragma": {"special": "resolve-partially"}
      }
    }
  , "tgz_repo":
    { "repository":
      { "type": "archive"
      , "content": "${TGZ_REPO_CONTENT}"
      , "distfile": "tgz_repo.tar.gz"
      , "fetch": "http://non-existent.example.org/tgz_repo.tar.gz"
      , "mirrors":
        [ "http://non-existent.example.org/tgz_repo.tar.gz"
        , "http://127.0.0.1:${port_num}/tgz_repo.tar.gz"
        ]
      , "sha256": "${TGZ_REPO_SHA256}"
      , "sha512": "${TGZ_REPO_SHA512}"
      , "subdir": "root/baz"
      , "pragma": {"special": "ignore"}
      }
    }
  , "git_repo":
    { "repository":
      { "type": "git"
      , "repository": "http://non-existent.example.org/dummy.git"
      , "mirrors":
        [ "http://non-existent.example.org/dummy.git"
        , "${GIT_ROOT}"
        ]
      , "branch": "test"
      , "commit": "${GIT_REPO_COMMIT}"
      , "subdir": "foo"
      }
    }
  , "distdir_repo":
    { "repository":
      { "type": "distdir"
      , "repositories": ["zip_repo", "tgz_repo", "git_repo"]
      }
    }
  }
}
EOF

echo "Test mirrors fetch"
rm -rf ${BUILDROOT} ${DISTFILES}/*
${JUST_MR_CPP} -C test-repos.json --norc --local-build-root ${BUILDROOT} ${DISTDIR_ARGS} -j 32 fetch --all -o ${DISTFILES}

echo "Test mirrors setup"
rm -rf ${BUILDROOT} ${DISTFILES}/*
CONFIG_CPP=$(${JUST_MR_CPP} -C test-repos.json --norc --local-build-root ${BUILDROOT} ${DISTDIR_ARGS} -j 32 setup --all)
if [ ! -s "${CONFIG_CPP}" ]; then
    exit 1
fi

### Using .just-local specifications

cat > test-repos.json <<EOF
{ "repositories":
  { "git_repo_1":
    { "repository":
      { "type": "git"
      , "repository": "http://non-existent.example.org/dummy.git"
      , "branch": "test"
      , "commit": "${GIT_REPO_COMMIT}"
      , "subdir": "foo"
      }
    }
  , "git_repo_2":
    { "repository":
      { "type": "git"
      , "repository": "http://non-existent.example.org/dummy.git"
      , "branch": "test"
      , "commit": "${GIT_REPO_COMMIT}"
      , "subdir": "."
      }
    }
  , "zip_repo":
    { "repository":
      { "type": "zip"
      , "content": "${ZIP_REPO_CONTENT}"
      , "distfile": "zip_repo.zip"
      , "fetch": "http://non-existent.example.org/zip_repo.zip"
      , "sha256": "${ZIP_REPO_SHA256}"
      , "sha512": "${ZIP_REPO_SHA512}"
      , "subdir": "root"
      , "pragma": {"special": "resolve-partially"}
      }
    }
  , "tgz_repo":
    { "repository":
      { "type": "archive"
      , "content": "${TGZ_REPO_CONTENT}"
      , "distfile": "tgz_repo.tar.gz"
      , "fetch": "http://non-existent.example.org:${port_num}/tgz_repo.tar.gz"
      , "sha256": "${TGZ_REPO_SHA256}"
      , "sha512": "${TGZ_REPO_SHA512}"
      , "subdir": "root/baz"
      , "pragma": {"special": "ignore"}
      }
    }
  }
}
EOF

cat > just-local.json <<EOF
{ "local mirrors":
  { "http://non-existent.example.org/dummy.git":
    [ "http://non-existent.example.org/dummy.git"
    , "${GIT_ROOT}"
    ]
  , "http://non-existent.example.org/zip_repo.zip":
    [ "http://non-existent.example.org/zip_repo.zip"
    , "http://127.0.0.1:${port_num}/zip_repo.zip"
    ]
  , "http://non-existent.example.org:${port_num}/tgz_repo.tar.gz":
    [ "http://non-existent.example.org/tgz_repo.tar.gz"
    , "http://127.0.0.1:${port_num}/tgz_repo.tar.gz"
    ]
  }
}
EOF

echo "Test 'local mirrors' fetch"
rm -rf ${BUILDROOT} ${DISTFILES}/*
${JUST_MR_CPP} -C test-repos.json --norc --local-build-root ${BUILDROOT} --checkout-locations just-local.json -j 32 fetch --all -o ${DISTFILES}

echo "Test 'local mirrors' setup"
rm -rf ${BUILDROOT} ${DISTFILES}/*
CONFIG_CPP=$(${JUST_MR_CPP} -C test-repos.json --norc --local-build-root ${BUILDROOT} --checkout-locations just-local.json -j 32 setup --all)
if [ ! -s "${CONFIG_CPP}" ]; then
    exit 1
fi

### Create second "state server" that writes access file if used

readonly STATE_SERVER="${ROOT}/utils/null-server"
readonly STATE_SERVER_ROOT="${TEST_TMPDIR}/state-server-root"

mkdir -p "${STATE_SERVER_ROOT}"
"${STATE_SERVER}" "${STATE_SERVER_ROOT}/port" "${STATE_SERVER_ROOT}/access" & state_server_pid=$!
trap "kill $server_pid" INT TERM EXIT
trap "server_cleanup ${server_pid} ${state_server_pid}" INT TERM EXIT
while [ '!' -f "${STATE_SERVER_ROOT}/port" ]
do
    sleep 1s
done
# get port of "state server"
state_port_num="$(cat ${STATE_SERVER_ROOT}/port)"
if [ -z "${state_port_num}" ]; then
    exit 1
fi

### Use .just-local with 'preferred hostnames' favoring host '127.0.0.1' over
### 'localhost' aka the "state server".

cat > test-repos.json <<EOF
{ "repositories":
  { "git_repo_1":
    { "repository":
      { "type": "git"
      , "repository": "http://localhost:${state_port_num}/dummy.git"
      , "branch": "test"
      , "commit": "${GIT_REPO_COMMIT}"
      , "subdir": "foo"
      }
    }
  , "git_repo_2":
    { "repository":
      { "type": "git"
      , "repository": "http://localhost:${state_port_num}/dummy.git"
      , "branch": "test"
      , "commit": "${GIT_REPO_COMMIT}"
      , "subdir": "."
      }
    }
  , "zip_repo":
    { "repository":
      { "type": "zip"
      , "content": "${ZIP_REPO_CONTENT}"
      , "distfile": "zip_repo.zip"
      , "fetch": "http://localhost:${state_port_num}/zip_repo.zip"
      , "mirrors": ["http://127.0.0.1:${port_num}/zip_repo.zip"]
      , "sha256": "${ZIP_REPO_SHA256}"
      , "sha512": "${ZIP_REPO_SHA512}"
      , "subdir": "root"
      , "pragma": {"special": "resolve-partially"}
      }
    }
  , "tgz_repo":
    { "repository":
      { "type": "archive"
      , "content": "${TGZ_REPO_CONTENT}"
      , "distfile": "tgz_repo.tar.gz"
      , "fetch": "http://localhost:${state_port_num}/tgz_repo.tar.gz"
      , "mirrors": ["http://127.0.0.1:${port_num}/tgz_repo.tar.gz"]
      , "sha256": "${TGZ_REPO_SHA256}"
      , "sha512": "${TGZ_REPO_SHA512}"
      , "subdir": "root/baz"
      , "pragma": {"special": "ignore"}
      }
    }
  }
}
EOF

cat > just-local.json <<EOF
{ "local mirrors":
  {"http://localhost:${state_port_num}/dummy.git": ["${GIT_ROOT}"]}
, "preferred hostnames": ["127.0.0.1", "localhost"]
}
EOF

echo "Test 'preferred hostnames' fetch"
rm -rf ${BUILDROOT} ${DISTFILES}/*
${JUST_MR_CPP} -C test-repos.json --norc --local-build-root ${BUILDROOT} --checkout-locations just-local.json -j 32 fetch --all -o ${DISTFILES}
if [ -f "${STATE_SERVER_ROOT}/access" ]; then
    cat "${STATE_SERVER_ROOT}/access"
    exit 1
fi

echo OK
