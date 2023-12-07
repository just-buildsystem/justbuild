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


readonly JUST_MR="${PWD}/bin/mr-tool-under-test"
readonly LBR="${TEST_TMPDIR}/local-build-root"
readonly LOG_DIR="${TEST_TMPDIR}/put-log-files-here"
mkdir -p "${LOG_DIR}"
readonly RC_DIR="${TEST_TMPDIR}/etc/just-mr-rc-files"
mkdir -p "${RC_DIR}"
readonly TOOLS_DIR="${TEST_TMPDIR}/tools"
mkdir -p "${TOOLS_DIR}"
readonly PARSE_DIR="${TEST_TMPDIR}/what-was-forwarded"
mkdir -p "${PARSE_DIR}"


readonly SAMPLE_RC="${RC_DIR}/sample.rc"
cat > "${SAMPLE_RC}" <<EOF
{ "log limit": 4
, "log files":
  [ {"root": "system", "path": "${LOG_DIR#/}/rc1.log"}
  , {"root": "system", "path": "${LOG_DIR#/}/rc2.log"}
  ]
, "local launcher": ["env", "SET_IN_RC=true"]
, "just files":
  { "config": [{"root": "workspace", "path": "sample-config.json"}]
  , "endpoint-configuration": [{"root": "workspace", "path": "endpoint.json"}]
  }
}
EOF
cat "${SAMPLE_RC}"
echo

# We verify the taken defaults by inspecting the arguments forwarded
# to the launched program.
readonly PARSE="${TOOLS_DIR}/parse.py"
cat > "${PARSE}" <<'EOF'
#!/usr/bin/env python3

import json
import os
import sys
from argparse import ArgumentParser



parser = ArgumentParser()
parser.add_argument("-C", dest="repository_config")
parser.add_argument("-c", "--config", dest="build_config")
parser.add_argument("--endpoint-configuration", dest="endpoint")
parser.add_argument("--local-build-root", dest="local_build_root")
parser.add_argument("-L","--local-launcher", dest="local_launcher")
parser.add_argument("-f", "--log-file", dest="log_file",
                    action="append", default=[])
parser.add_argument("--log-limit", dest="log_limit")
parser.add_argument("--log-append", dest="log_append",
                    action="store_true", default=False)
parser.add_argument("-D", "--defines", dest="defines",
                    action="append", default=[])
(options, args) = parser.parse_known_args(sys.argv[2:])

target_dir=args[-1]
with open(os.path.join(target_dir, "defines"), "w") as f:
  f.write(json.dumps([json.loads(x) for x in options.defines]))
with open(os.path.join(target_dir, "log-limit"), "w") as f:
  f.write(json.dumps(options.log_limit))
with open(os.path.join(target_dir, "log-file"), "w") as f:
  f.write(json.dumps(options.log_file))
if options.build_config:
  with open(os.path.join(target_dir, "config"), "w") as f:
    f.write(json.dumps(options.build_config))
else:
  if os.path.exists(os.path.join(target_dir, "config")):
    os.unlink(os.path.join(target_dir, "config"))
if options.endpoint:
  with open(os.path.join(target_dir, "endpoint"), "w") as f:
    f.write(json.dumps(options.endpoint))
else:
  if os.path.exists(os.path.join(target_dir, "endpoint")):
    os.unlink(os.path.join(target_dir, "endpoint"))
with open(os.path.join(target_dir, "launcher"), "w") as f:
  if options.local_launcher:
    f.write(options.local_launcher)
  else:
    f.write("null")
EOF
chmod 755 "${PARSE}"


touch ROOT
cat > repos.json <<'EOF'
{"repositories": {"": {"repository": {"type": "file", "path": "."}}}}
EOF

## log limit

#  rc is honored
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" build "${PARSE_DIR}" 2>&1
test "$(cat "${PARSE_DIR}/log-limit")" = '"4"'
# command line overrides
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" --log-limit 5 build "${PARSE_DIR}" 2>&1
test "$(cat "${PARSE_DIR}/log-limit")" = '"5"'
# value equal to default is discarded
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" --log-limit 3 build "${PARSE_DIR}" 2>&1
test "$(cat "${PARSE_DIR}/log-limit")" = 'null'


## log files

# rc are taken
rm -f "${LOG_DIR}/rc1.log"
rm -f "${LOG_DIR}/rc2.log"
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" build "${PARSE_DIR}" 2>&1
test -f "${LOG_DIR}/rc1.log"
test -f "${LOG_DIR}/rc2.log"
test $(jq ". == [\"${LOG_DIR}/rc1.log\", \"${LOG_DIR}/rc2.log\"]" "${PARSE_DIR}/log-file") = "true"

# command-line log files are added
rm -f "${LOG_DIR}/rc1.log"
rm -f "${LOG_DIR}/rc2.log"
rm -f "${LOG_DIR}/cli.log"
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" -f "${LOG_DIR}/cli.log" build "${PARSE_DIR}" 2>&1
test -f "${LOG_DIR}/rc1.log"
test -f "${LOG_DIR}/rc2.log"
test -f "${LOG_DIR}/cli.log"
test $(jq "sort == [\"${LOG_DIR}/cli.log\", \"${LOG_DIR}/rc1.log\", \"${LOG_DIR}/rc2.log\"]" "${PARSE_DIR}/log-file") = "true"

## launcher

# rc is honored
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" build "${PARSE_DIR}" 2>&1
test $(jq '. == ["env", "SET_IN_RC=true"] ' "${PARSE_DIR}/launcher") = "true"

# command-line takes precedence
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" -L '["cmd", "line", "launcher"]' \
             build "${PARSE_DIR}" 2>&1
test $(jq '. == ["cmd", "line", "launcher"] ' "${PARSE_DIR}/launcher") = "true"

# value equal to defaul is discared
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" -L '["env", "--"]' \
             build "${PARSE_DIR}" 2>&1
test "$(cat "${PARSE_DIR}/launcher")" = 'null'

## Command-line -D

# ignored on non-build commands
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${PARSE}" \
             -D 'this is not json' version "${PARSE_DIR}" 2>&1
test $(jq '. == [] ' "${PARSE_DIR}/defines") = "true"

# not forwarded, if empty
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${PARSE}" \
             -D '{}' build "${PARSE_DIR}" 2>&1
test $(jq '. == [] ' "${PARSE_DIR}/defines") = "true"

# combined on forwarding
"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${PARSE}" \
             -D '{"foo": "bar"}' -D '{"baz": "baz"}' -D '{"foo": "override"}' \
             build "${PARSE_DIR}" 2>&1
test $(jq '. == [ {"foo": "override", "baz": "baz"}] ' "${PARSE_DIR}/defines") = true

# but passed arguments are given separately

"${JUST_MR}" --norc --local-build-root "${LBR}" --just "${PARSE}" \
             -D '{"foo": "bar"}' -D '{"baz": "baz"}' \
             build -D '{"foo": "override"}' -D '{"x": "y"}' "${PARSE_DIR}" 2>&1
test $(jq '. == [ {"foo": "bar", "baz": "baz"}, {"foo": "override"}, {"x": "y"}] ' "${PARSE_DIR}/defines") = true

## -c

# honored from rc-file if present
touch sample-config.json
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" build "${PARSE_DIR}" 2>&1
[ -f "${PARSE_DIR}/config" ]

# not honored for non-analysing subcommands
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" install-cas "${PARSE_DIR}" 2>&1
[ -f "${PARSE_DIR}/config" ] && exit 1 || :

# not considered an error if not present
rm -f sample-config.json
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" build "${PARSE_DIR}" 2>&1
[ -f "${PARSE_DIR}/config" ] && exit 1 || :

# not considered, if key not present in rc
cat > tmprc.json <<'EOF'
{"just files": {"unrelated": 123}}
EOF
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc tmprc.json build "${PARSE_DIR}" 2>&1
[ -f "${PARSE_DIR}/config" ] && exit 1 || :

## --endpoint-configuration

# honored from rc-file if present
touch endpoint.json
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" build "${PARSE_DIR}" 2>&1
[ -f "${PARSE_DIR}/endpoint" ]

# not honored for non-endpoint-specific subcommands
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" install-cas "${PARSE_DIR}" 2>&1
[ -f "${PARSE_DIR}/endpoint" ] && exit 1 || :

# not considered an error if not present
rm -f endpoint.json
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc "${SAMPLE_RC}" build "${PARSE_DIR}" 2>&1
[ -f "${PARSE_DIR}/endpoint" ] && exit 1 || :

# not considered, if key not present in rc
cat > tmprc.json <<'EOF'
{"just files": {"unrelated": 123}}
EOF
"${JUST_MR}" --local-build-root "${LBR}" --just "${PARSE}" \
             --rc tmprc.json build "${PARSE_DIR}" 2>&1
[ -f "${PARSE_DIR}/endpoint" ] && exit 1 || :

echo OK
