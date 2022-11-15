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

TOOL=$(realpath ./bin/tool-under-test)
mkdir -p .root
BUILDROOT=$(realpath .root)
mkdir -p out
OUTDIR=$(realpath out)

mkdir src
cd src
touch ROOT
cat > TARGETS <<'EOI'
{ "configured":
  { "type": "configure"
  , "arguments_config": ["TARGET", "FOO", "BAR"]
  , "target": {"type": "var", "name": "TARGET"}
  , "config":
    { "type": "let*"
    , "bindings":
      [ ["FOO", {"type": "var", "name": "FOO", "default": "foo"}]
      , ["BAR", {"type": "var", "name": "BAR", "default": "bar"}]
      , [ "FOOBAR"
        , { "type": "join"
          , "$1":
            [{"type": "var", "name": "FOO"}, {"type": "var", "name": "BAR"}]
          }
        ]
      ]
    , "body": {"type": "env", "vars": ["FOO", "BAR", "FOOBAR"]}
    }
  }
, "foobar":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "arguments_config": ["FOOBAR"]
  , "cmds":
    [ { "type": "join"
      , "separator": " "
      , "$1":
        [ "echo"
        , "-n"
        , {"type": "join_cmd", "$1": [{"type": "var", "name": "FOOBAR"}]}
        , "> out.txt"
        ]
      }
    ]
  }
, "bar":
  { "type": "file_gen"
  , "arguments_config": ["BAR"]
  , "name": "out.txt"
  , "data": {"type": "var", "name": "BAR"}
  }
}
EOI
echo -n unrelated > out.txt


echo
${TOOL} install --local-build-root "${BUILDROOT}" -o "${OUTDIR}" \
        -D '{"TARGET": "foobar", "FOO": "_F_O_O_"}' configured 2>&1
echo
cat "${OUTDIR}/out.txt"
echo
[ $(cat "${OUTDIR}/out.txt") = "_F_O_O_bar" ]

echo
${TOOL} install --local-build-root "${BUILDROOT}" -o "${OUTDIR}" \
        -D '{"TARGET": "bar", "FOO": "_F_O_O_"}' configured 2>&1
echo
cat "${OUTDIR}/out.txt"
echo
[ $(cat "${OUTDIR}/out.txt") = "bar" ]

echo
${TOOL} install --local-build-root "${BUILDROOT}" -o "${OUTDIR}" \
        -D '{"TARGET": "out.txt", "FOO": "_F_O_O_"}' configured 2>&1
echo
cat "${OUTDIR}/out.txt"
echo
[ $(cat "${OUTDIR}/out.txt") = "unrelated" ]


echo DONE
