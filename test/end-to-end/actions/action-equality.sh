#!/bin/sh
set -e

mkdir .tool-root
touch ROOT
cat > TARGETS <<'EOI'
{ "foo":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["echo Hello World > out.txt"]
  }
, "bar":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["echo Hello World > out.txt"]
  }
, "baz":
  { "type": "generic"
  , "outs": ["out.txt"]
  , "cmds": ["echo -n Hello > out.txt && echo ' World' >> out.txt"]
  }
, "foo upper":
  { "type": "generic"
  , "deps": ["foo"]
  , "outs": ["upper.txt"]
  , "cmds": ["cat out.txt | tr a-z A-Z > upper.txt"]
  }
, "bar upper":
  { "type": "generic"
  , "deps": ["bar"]
  , "outs": ["upper.txt"]
  , "cmds": ["cat out.txt | tr a-z A-Z > upper.txt"]
  }
, "baz upper":
  { "type": "generic"
  , "deps": ["baz"]
  , "outs": ["upper.txt"]
  , "cmds": ["cat out.txt | tr a-z A-Z > upper.txt"]
  }
, "ALL":
  { "type": "install"
  , "files":
    {"foo.txt": "foo upper", "bar.txt": "bar upper", "baz.txt": "baz upper"}
  }
}
EOI


bin/tool-under-test build -J 1 --local_build_root .tool-root -f build.log 2>&1
cat build.log
echo
grep 'Processed.* 4 actions' build.log
grep '1 cache hit' build.log

echo
bin/tool-under-test analyse --dump_graph graph.json 2>&1
echo
matching_targets=$(cat graph.json | jq -acM '.actions | [ .[] | .origins | [ .[] | .target]] | sort')
echo "${matching_targets}"
[ "${matching_targets}" = '[[["@","","","bar"],["@","","","foo"]],[["@","","","bar upper"],["@","","","foo upper"]],[["@","","","baz"]],[["@","","","baz upper"]]]' ]
