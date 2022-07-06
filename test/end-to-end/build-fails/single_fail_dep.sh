#!/bin/bash

set -eu

readonly NUM_DEPS=100
readonly FAIL_DEP=42

create_target() {
  local ID=$1
  if [ $ID = $FAIL_DEP ]; then
    # failing target
    echo '{"type": "generic", "cmds": ["false"], "out_dirs": ["out_'$ID'"]}'
  else
    # success target
    echo '{"type": "generic", "cmds": ["echo '$ID' > out_'$ID'/id"], "out_dirs": ["out_'$ID'"]}'
  fi
}

mkdir -p .buildroot

touch ROOT

cat <<EOF > TARGETS
{ $(for i in $(seq $NUM_DEPS); do
      if [ $i = 1 ]; then echo -n ' '; else echo -n '  , '; fi;
      echo '"dep_'$i'": '$(create_target $i); done)
, "main":
  { "type": "generic"
  , "cmds": ["cat out_*/id | sort -n > id"]
  , "outs": ["id"]
  , "deps":
    [ $(for i in $(seq $NUM_DEPS); do
          if [ $i = 1 ]; then echo -n ' '; else echo -n '    , '; fi;
          echo '"dep_'$i'"';
        done)
    ]
  }
}
EOF

cat TARGETS

echo "main" >&2
bin/tool-under-test build --local-build-root .buildroot main && exit 1 || [ $? = 1 ]
echo "done" >&2

exit
