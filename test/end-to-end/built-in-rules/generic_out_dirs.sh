#!/bin/bash
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

touch ROOT
mkdir -p lcl-build

cat <<EOF > TARGETS
{ "gen_out_dirs":
  {"type": "generic", "cmds": ["echo foo > out/foo.txt"], "out_dirs": ["out"]}
, "read_out_dirs":
  { "type": "generic"
  , "cmds": ["cat out/foo.txt > bar.txt"]
  , "outs": ["bar.txt"]
  , "deps": ["gen_out_dirs"]
  }
, "missing_outs_and_out_dirs":
  {"type": "generic", "cmds": ["echo foo > out/foo.txt"]}
, "out_dirs_contains_a_file":
  {"type": "generic", "cmds": ["echo foo > foo.txt"], "out_dirs": ["foo.txt"]}
, "outs_contains_a_dir":
  { "type": "generic"
  , "cmds": ["mkdir out", "echo foo > out/foo.txt"]
  , "outs": ["out"]
  }
, "collision":
  { "type": "generic"
  , "cmds": ["touch foo"]
  , "outs": ["foo"]
  , "out_dirs": ["foo"]
  }
}
EOF

echo "read_out_dirs" >&2
bin/tool-under-test build -L '["env", "PATH='"${PATH}"'"]' --local-build-root lcl-build  read_out_dirs
echo "done" >&2

echo "missing_outs_and_out_dirs" >&2
# at least one of "outs" or "out_dirs" must be declared. we expect to fail during the analysis phase
bin/tool-under-test analyse --local-build-root lcl-build --log-limit 0 -f test.log missing_outs_and_out_dirs && exit 1 || :
grep 'outs' test.log && grep 'out_dirs' test.log
echo "done" >&2

echo "out_dirs_contains_a_file" >&2
# the directories listed in "out_dirs" are created before the cmd is exectuted
# so, we expect the shell to complain that it cannot generate file "foo.txt"
# because it is a directory. We don't grep the shell error message because it can
# varay.
# the analysis phase should run fine
bin/tool-under-test analyse --local-build-root lcl-build out_dirs_contains_a_file
echo "analysis ok" >&2
bin/tool-under-test build -L '["env", "PATH='"${PATH}"'"]' --local-build-root lcl-build out_dirs_contains_a_file && exit 1 || :
echo "done" >&2

echo "outs_contains_a_dir" >&2
# we declared an output file "out", which is actually a tree 
# if we don't creat directory out, the shell will also complain about nonexisting directory "out"
# anlysis should run fine
bin/tool-under-test analyse --local-build-root lcl-build outs_contains_a_dir
echo "analysis ok" >&2
bin/tool-under-test build -L '["env", "PATH='"${PATH}"'"]' --local-build-root lcl-build -f test.log outs_contains_a_dir && exit 1 || :
# grep 'ERROR' test.log | grep 'output file'
echo "done" >&2

echo "collision" >&2
# we expect an error during the analysis phase because outs and out_dirs must be disjoint
bin/tool-under-test analyse --local-build-root lcl-build --log-limit 0 -f test.log collision && exit 1 || :
grep 'disjoint' test.log
echo "done" >&2
exit
