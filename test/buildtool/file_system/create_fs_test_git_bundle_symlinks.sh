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

set -e

# ---
# Structure of test_repo_symlinks:
# ---
# <root>   <--kTreeSymId: e00aa80fd1600090930c7ec0b7146028693074bf (tree)
# |
# +--bar     <--kBarId: same as in test_repo.bundle (blob)
# +--foo     <--kFooId: same as in test_repo.bundle (blob)
# +--baz_l   <-- kBazLinkId: 3f9538666251333f5fa519e01eb267d371ca9c78 (blob)
# +--baz     <--kBazSymId: 9df184c5a0b324f488176432724deae97adaace8 (tree)
# |  +--bar
# |  +--foo
# |  +--bar_l   <-- kBazBarLinkId: 79264abecd108745abb4086427ac988c7df7b639 (blob)
# |  +--foo_l   <-- kBazFooLinkId: 7013e0db1095e8276a6e249830d999aecb7abd3d (blob)
# |
# ---
#
# foo is a regular file
# bar is an executable
#
# foo_l is a symlink to ../foo
# bar_l is a symlink to ../bar
# baz_l is a symlink to ./baz
#
# Bundle name: test_repo_symlinks.bundle
#

# create the folder structure
mkdir -p test_repo_symlinks
cd test_repo_symlinks

printf %s "foo" >> foo  # no newline
printf %s "bar" >> bar  # no newline
chmod +x bar
mkdir -p baz/
cp foo baz/foo
cp bar baz/bar
ln -s ../foo baz/foo_l
ln -s ../bar baz/bar_l
ln -s baz baz_l

# create the repo
git init > /dev/null 2>&1
git checkout -q -b master
git config user.name "Nobody"
git config user.email "nobody@example.org"

git add .
GIT_AUTHOR_DATE="1970-01-01T00:00Z" GIT_COMMITTER_DATE="1970-01-01T00:00Z" git commit -m "First commit" > /dev/null

# create the git bundle
git bundle create ../data/test_repo_symlinks.bundle HEAD master

# unlink the symlinks ourselves, to avoid broken symlinks issues on cleanup
unlink baz_l
unlink baz/bar_l
unlink baz/foo_l
