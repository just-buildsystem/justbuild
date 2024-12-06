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

ROOT="$(pwd)"
JUST="${ROOT}/bin/tool-under-test"
JUST_MR="${ROOT}/bin/mr-tool-under-test"
LBR="${TEST_TMPDIR}/build-root"

mkdir work
cd work
touch ROOT

cat > RULES <<'EOF'
{ "test":
  { "tainted": ["test"]
  , "string_fields": ["cmds"]
  , "expression":
    { "type": "let*"
    , "bindings":
      [ [ "cmd"
        , { "type": "join"
          , "separator": "\n"
          , "$1": {"type": "FIELD", "name": "cmds"}
          }
        ]
      , [ "shell cmd"
        , { "type": "join_cmd"
          , "$1": ["sh", "-c", {"type": "var", "name": "cmd"}]
          }
        ]
      , [ "test cmd"
        , [ "sh"
          , "-c"
          , { "type": "join"
            , "separator": " "
            , "$1":
              [ "echo FAIL > RESULT;"
              , {"type": "var", "name": "shell cmd"}
              , " && echo PASS > RESULT;"
              , "if [ $(cat RESULT) != PASS ] ; then exit 1 ; fi"
              ]
            }
          ]
        ]
      , [ "result"
        , { "type": "ACTION"
          , "cmd": {"type": "var", "name": "test cmd"}
          , "outs": ["RESULT"]
          , "may_fail": ["test"]
          , "fail_message": "TeStFaIlUrE"
          }
        ]
      ]
    , "body":
      {"type": "RESULT", "artifacts": {"type": "var", "name": "result"}}
    }
  }
}
EOF

cat > TARGETS <<'EOF'
{ "verbose":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds":
    [ "echo -n Running the VeR"
    , "echo bOsE command now"
    , "echo Hello WoRlD > out.txt"
    ]
  }
, "test": {"type": "test", "cmds": ["echo TeStRuN", "false"]}
, "":
  { "type": "install"
  , "tainted": ["test"]
  , "files": {"verbose": "verbose", "test": "test"}
  }
}
EOF


echo FAIL > "${TEST_TMPDIR}/result"
FAIL_HASH=$("${JUST}" add-to-cas --local-build-root "${LBR}" "${TEST_TMPDIR}/result")

"${JUST}" build --local-build-root "${LBR}" -f file.log \
          -L '["env", "PATH='"${PATH}"'"]' \
          --restrict-stderr-log-limit 1 2>console.log || :

echo
echo Console
cat console.log
echo
echo Log file
cat file.log

echo
echo verifying log limits ...
echo

# stdout/stderr of actions are reported at INFO level (included in the default,
# but lower than WARN, which the console is restricted to.
grep VeRbOsE file.log
echo
grep VeRbOsE console.log && exit 1 || :
echo
grep TeStRuN file.log
echo
grep TeStRuN console.log && exit 1 || :

# Failed actions are reported at WARN level, hence should be present in both
echo
grep TeStFaIlUrE file.log
grep TeStFaIlUrE console.log
# Also, their outputs should be reported
grep "RESULT.*${FAIL_HASH}" file.log
grep "RESULT.*${FAIL_HASH}" console.log

echo
echo
echo Same test using the launcher
echo
touch ROOT
cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": "."}}}}
EOF
rm -f file.log console.log
"${JUST_MR}" --norc --just "${JUST}" --local-build-root "${LBR}" -f file.log \
          -L '["env", "PATH='"${PATH}"'"]' \
          --restrict-stderr-log-limit 1 build 2>console.log || :
echo
echo Console
cat console.log
echo
echo Log file
cat file.log

echo
echo verifying log limits ...
echo
# The information of the launch invocation is logged at INFO level, hence should
# be visible in the file log, but not in the console log
grep tool-under-test file.log
echo
grep tool-under-test console.log && exit 1 || :
echo
# stdout/stderr of actions are reported at INFO level (included in the default,
# but lower than WARN, which the console is restricted to.
grep VeRbOsE file.log
echo
grep VeRbOsE console.log && exit 1 || :
echo
grep TeStRuN file.log
echo
grep TeStRuN console.log && exit 1 || :

# Failed actions are reported at WARN level, hence should be present in both
echo
grep TeStFaIlUrE file.log
grep TeStFaIlUrE console.log


echo
echo
echo Same test using the launcher, with information taken from rc file
echo
cat > rc.json <<EOF
{ "local build root": {"root": "system", "path": "${LBR}"}
, "just": {"root": "system", "path": "${JUST}"}
, "restrict stderr log limit": 1
, "local launcher": ["env", "PATH=${PATH}"]
}
EOF
cat rc.json
rm -f file.log console.log
"${JUST_MR}" --rc rc.json -f file.log build 2>console.log || :
echo
echo Console
cat console.log
echo
echo Log file
cat file.log


echo
echo verifying log limits ...
echo
# The information of the launch invocation is logged at INFO level, hence should
# be visible in the file log, but not in the console log
grep tool-under-test file.log
echo
grep tool-under-test console.log && exit 1 || :
echo
# stdout/stderr of actions are reported at INFO level (included in the default,
# but lower than WARN, which the console is restricted to.
grep VeRbOsE file.log
echo
grep VeRbOsE console.log && exit 1 || :
echo
grep TeStRuN file.log
echo
grep TeStRuN console.log && exit 1 || :

# Failed actions are reported at WARN level, hence should be present in both
echo
grep TeStFaIlUrE file.log
grep TeStFaIlUrE console.log
# Also, their outputs should be reported
grep "RESULT.*${FAIL_HASH}" file.log
grep "RESULT.*${FAIL_HASH}" console.log

echo
echo OK
