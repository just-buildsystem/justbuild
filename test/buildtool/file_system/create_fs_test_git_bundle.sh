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

set -e

# ---
# Structure of test_repo:
# ---
# <root>   <--kTreeId: c610db170fbcad5f2d66fe19972495923f3b2536 (tree)
# |
# +--bar   <--kBarId: ba0e162e1c47469e3fe4b393a8bf8c569f302116 (blob)
# +--foo   <--kFooId: 19102815663d23f8b75a47e7a01965dcdc96468c (blob)
# +--baz   <--kBazId: 27b32561185c2825150893774953906c6daa6798 (tree)
# |  +--bar
# |  +--foo
# |
# ---
#
# kCommitId: e4fc610c60716286b98cf51ad0c8f0d50f3aebb5 (commit)
#
# foo is a regular file
# bar is an executable
#
# Bundle name: test_repo.bundle
#

# create the folder structure
mkdir -p test_repo
cd test_repo

printf %s "foo" >> foo  # no newline
printf %s "bar" >> bar  # no newline
chmod +x bar
mkdir -p baz
cp foo baz/foo
cp bar baz/bar

# create the repo
git init > /dev/null 2>&1
git checkout -q -b master
git config user.name "Nobody"
git config user.email "nobody@example.org"

git add .
GIT_AUTHOR_DATE="1970-01-01T00:00Z" GIT_COMMITTER_DATE="1970-01-01T00:00Z" git commit -m "First commit" > /dev/null

# create the git bundle
git bundle create ../data/test_repo.bundle HEAD master
