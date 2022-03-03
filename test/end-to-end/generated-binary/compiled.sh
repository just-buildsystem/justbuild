#!/bin/sh
set -e

mkdir .tool-root
touch ROOT
cat > TARGETS <<'EOI'
{ "program outputs": {"type": "generated files", "width": ["16"]}
, "ALL":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["cat $(ls out-*.txt | sort) > out.txt"]
  , "deps": ["program outputs"]
  }
}
EOI

echo
echo "Analysing"
bin/tool-under-test analyse --dump_graph graph.json 2>&1

echo
echo "Building"
bin/tool-under-test install -o out --local_build_root .tool-root -J 16 2>&1
