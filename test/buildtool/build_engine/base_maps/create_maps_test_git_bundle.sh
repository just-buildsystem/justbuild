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
# <root>
# |
# +--src                <--kSrcTreeId
# |  +--file
# |  +--foo
# |  |  +--bar
# |  |  |  +--file
# |
# +--expr               <--kExprTreeId
# |  +--EXRESSIONS
# |  +--readers
# |  |  +--EXPRESSIONS
# |
# +--rule               <--kRuleTreeId
# |  +--RULES
# |  +--composers
# |  |  +--EXPRESSIONS
# |
# +--json               <--kJsonTreeId
# |  +--data_json
# |  |  +--bad.json
# |  |  +--foo.json
# |
# ---
# Repository root folder: test_repo
# Bundle name: test_repo.bundle
# Metadata: test_repo.json
#

# create the root folder
mkdir -p test_repo
cd test_repo

# create the repo
git init > /dev/null 2>&1
git checkout -q -b master
git config user.name "Nobody"
git config user.email "nobody@example.org"

# create src tree
mkdir -p src/foo/bar
touch src/file
touch src/foo/bar/file

# create src commit
git add .
GIT_AUTHOR_DATE="1970-01-01T00:00Z" GIT_COMMITTER_DATE="1970-01-01T00:00Z" git commit -m "src" > /dev/null

# create expr tree
mkdir -p expr/readers
cat <<EOF > expr/EXPRESSIONS
{
  "test_expression_literal": {
    "expression": "foo"
  },
  "test_read_vars": {
    "vars": ["FOO"],
    "expression": {
      "type": "var",
      "name": "FOO"
    }
  },
  "test_call_import": {
    "vars": ["FOO"],
    "imports": {
      "read_foo": [
        "readers",
        "real_foo_reader"
      ]
    },
    "expression": {
      "type": "CALL_EXPRESSION",
      "name": "read_foo"
    }
  },
  "test_overwrite_import": {
    "vars": ["FOO"],
    "imports": {
      "read_foo": [
        "readers",
        "proxy_foo_reader"
      ]
    },
    "expression": {
      "type": "CALL_EXPRESSION",
      "name": "read_foo"
    }
  },
  "test_missing_vars": {
    "expression": {
      "type": "var",
      "name": "FOO"
    }
  },
  "test_missing_imports": {
    "expression": {
      "type": "CALL_EXPRESSION",
      "name": "read_foo"
    }
  },
  "test_malformed_function": "not_an_object",
  "test_malformed_expression": {
    "missing_expression": {}
  },
  "test_malformed_vars": {
    "vars": "not_a_list",
    "expression": {
      "type": "empty_map"
    }
  },
  "test_malformed_imports": {
    "imports": "not_an_object",
    "expression": {
      "type": "empty_map"
    }
  }
}
EOF
cat <<EOF > expr/readers/EXPRESSIONS
{
  "proxy_foo_reader": {
    "vars": [
      "FOO"
    ],
    "imports": {
      "read_foo": "real_foo_reader"
    },
    "expression": {
      "type": "CALL_EXPRESSION",
      "name": "read_foo"
    }
  },
  "real_foo_reader": {
    "vars": [
      "FOO"
    ],
    "expression": {
      "type": "var",
      "name": "FOO"
    }
  }
}
EOF

# create expr commit
git add .
GIT_AUTHOR_DATE="1970-01-01T00:00Z" GIT_COMMITTER_DATE="1970-01-01T00:00Z" git commit -m "expr" > /dev/null

# create rule tree
mkdir -p rule/composers
cat <<EOF > rule/RULES
{
  "test_empty_rule": {
    "expression": {
      "type": "RESULT"
    }
  },
  "test_rule_fields": {
    "string_fields": ["foo"],
    "target_fields": ["bar"],
    "config_fields": ["baz"],
    "expression": {
      "type": "RESULT"
    }
  },
  "test_config_transitions_target_via_field": {
    "target_fields": ["target"],
    "config_transitions": {
      "target": [{
        "type": "empty_map"
      }]
    },
    "expression": {
      "type": "RESULT"
    }
  },
  "test_config_transitions_target_via_implicit": {
    "implicit": {
      "target": [
        ["module", "name"]
      ]
    },
    "config_transitions": {
      "target": [{
        "type": "empty_map"
      }]
    },
    "expression": {
      "type": "RESULT"
    }
  },
  "test_config_transitions_canonicalness": {
    "target_fields": ["foo", "bar"],
    "string_fields": ["quux", "corge"],
    "config_fields": ["grault", "garply"],
    "implicit": {
      "baz": [
        ["module", "name"]
      ],
      "qux": [
        ["module", "name"]
      ]
    },
    "config_transitions": {
      "bar": [{
        "type": "singleton_map",
        "key": "exists",
        "value": true
      }],
      "qux": [{
        "type": "singleton_map",
        "key": "defined",
        "value": true
      }]
    },
    "expression": {
      "type": "RESULT"
    }
  },
  "test_call_import": {
    "config_vars": ["FOO"],
    "imports": {
      "compose_foo": [
        "composers",
        "foo_composer"
      ]
    },
    "expression": {
      "type": "CALL_EXPRESSION",
      "name": "compose_foo"
    }
  },
  "test_string_kw_conflict": {
    "string_fields": ["foo", "type", "bar"],
    "expression": {
      "type": "RESULT"
    }
  },
  "test_target_kw_conflict": {
    "target_fields": ["foo", "arguments_config", "bar"],
    "expression": {
      "type": "RESULT"
    }
  },
  "test_config_kw_conflict": {
    "config_fields": ["foo", "type", "bar"],
    "expression": {
      "type": "RESULT"
    }
  },
  "test_implicit_kw_conflict": {
    "implicit": {
      "foo": [],
      "arguments_config": [],
      "bar": []
    },
    "expression": {
      "type": "RESULT"
    }
  },
  "test_string_target_conflict": {
    "string_fields": ["foo", "bar"],
    "target_fields": ["bar", "baz"],
    "expression": {
      "type": "RESULT"
    }
  },
  "test_target_config_conflict": {
    "target_fields": ["foo", "bar"],
    "config_fields": ["bar", "baz"],
    "expression": {
      "type": "RESULT"
    }
  },
  "test_config_implicit_conflict": {
    "config_fields": ["foo", "bar"],
    "implicit": {
      "bar": [
        ["module", "name"]
      ],
      "baz": [
        ["module", "name"]
      ]
    },
    "expression": {
      "type": "RESULT"
    }
  },
  "test_unknown_config_transitions_target": {
    "config_transitions": {
      "missing": [{
        "type": "empty_map"
      }]
    },
    "expression": {
      "type": "RESULT"
    }
  },
  "test_missing_config_vars": {
    "imports": {
      "compose_foo": [
        "composers",
        "foo_composer"
      ]
    },
    "expression": {
      "type": "CALL_EXPRESSION",
      "name": "compose_foo"
    }
  },
  "test_missing_imports": {
    "expression": {
      "type": "CALL_EXPRESSION",
      "name": "compose_foo"
    }
  },
  "test_malformed_rule": "not_an_object",
  "test_malformed_rule_expression": {
    "missing_expression": {
      "type": "RESULT"
    }
  },
  "test_malformed_target_fields": {
    "target_fields": "not_a_list",
    "expression": {
      "type": "RESULT"
    }
  },
  "test_malformed_string_fields": {
    "string_fields": "not_a_list",
    "expression": {
      "type": "RESULT"
    }
  },
  "test_malformed_config_fields": {
    "config_fields": "not_a_list",
    "expression": {
      "type": "RESULT"
    }
  },
  "test_malformed_implicit": {
    "implicit": "not_an_object",
    "expression": {
      "type": "RESULT"
    }
  },
  "test_malformed_implicit_entry": {
    "implicit": {
      "target": "not_a_list"
    },
    "expression": {
      "type": "RESULT"
    }
  },
  "test_malformed_implicit_entity_name": {
    "implicit": {
      "target": [
        ["module_without_name"]
      ]
    },
    "expression": {
      "type": "RESULT"
    }
  },
  "test_malformed_config_vars": {
    "config_vars": "not_a_list",
    "expression": {
      "type": "RESULT"
    }
  },
  "test_malformed_config_transitions": {
    "config_transitions": "not_an_object",
    "expression": {
      "type": "RESULT"
    }
  },
  "test_malformed_imports": {
    "imports": "not_an_object",
    "expression": {
      "type": "RESULT"
    }
  }
}
EOF
cat <<EOF > rule/composers/EXPRESSIONS
{
  "foo_composer": {
    "vars": [
      "FOO"
    ],
    "expression": {
      "type": "map_union",
      "\$1": [{
          "type": "singleton_map",
          "key": "type",
          "value": "RESULT"
        },
        {
          "type": "singleton_map",
          "key": "artifacts",
          "value": {
            "type": "singleton_map",
            "key": "foo",
            "value": {
              "type": "var",
              "name": "FOO"
            }
          }
        }
      ]
    }
  }
}
EOF

# create rule commit
git add .
GIT_AUTHOR_DATE="1970-01-01T00:00Z" GIT_COMMITTER_DATE="1970-01-01T00:00Z" git commit -m "rule" > /dev/null

# create json tree
mkdir -p json/data_json
cat <<EOF > json/data_json/bad.json
This is not JSON
EOF
cat <<EOF > json/data_json/foo.json
{
  "foo": "bar"
}
EOF

# create json commit
git add .
GIT_AUTHOR_DATE="1970-01-01T00:00Z" GIT_COMMITTER_DATE="1970-01-01T00:00Z" git commit -m "json" > /dev/null

# create the git bundle
git bundle create ../data/test_repo.bundle HEAD master
