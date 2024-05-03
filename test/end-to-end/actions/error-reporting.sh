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

set -e

readonly ROOT="$(pwd)"
readonly LBR="${TMPDIR}/local-build-root"
readonly OUT="${TMPDIR}/out"
readonly JUST="${ROOT}/bin/tool-under-test"
readonly LOCAL_TOOLS="${TMPDIR}/tools"

touch ROOT
cat > TARGETS <<'EOF'
{ "theTargetCausingTheFailure":
  { "type": "generic"
  , "arguments_config": ["foo", "bar", "magic"]
  , "outs": ["a.txt"]
  , "cmds":
    [ { "type": "join"
      , "$1":
        [ "echo -n stdout-of-; echo failing-target-"
        , {"type": "var", "name": "foo", "default": ""}
        , {"type": "var", "name": "bar", "default": ""}
        , "-$MAGIC_VAR"
        ]
      }
    , "touch a.txt"
    , "exit 42"
    ]
  , "env":
    { "type": "singleton_map"
    , "key": "MAGIC_VAR"
    , "value": {"type": "var", "name": "magic", "default": "unknown"}
    }
  }
}
EOF
cat TARGETS
echo

# Verify tat a failing build provides enough meaningful
# information at log-level ERROR.
mkdir -p "${OUT}"
"${JUST}" build --local-build-root "${LBR}" \
          -f "${OUT}/log" --log-limit 0 \
          -D '{"foo": "FOO", "irrelevant": "abc", "magic":"xyz"}' \
          2>&1 && exit 1 || :

# The exit code should be reported
grep 42 "${OUT}/log"

# The target name should be reported
grep theTargetCausingTheFailure "${OUT}/log"

# The pruned effective configuration should be reported in canonical
# compact form.
grep '{"foo":"FOO","magic":"xyz"}' "${OUT}/log"

# At default level we should also find stdout of the target
echo
"${JUST}" build --local-build-root "${LBR}" \
          -f "${OUT}/log.default" \
          -D '{"foo": "FOO", "irrelevant": "abc", "magic":"xyz"}' \
          2>&1 && exit 1 || :

grep stdout-of-failing-target-FOO-xyz "${OUT}/log.default"

# ... as well as command and environment in canonical compact form
grep 'echo -n stdout-of-;' "${OUT}/log.default"
grep '{"MAGIC_VAR":"xyz"}' "${OUT}/log.default"

echo OK
