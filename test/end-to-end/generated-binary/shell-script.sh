#!/bin/sh
set -e

mkdir .tool-root
touch ROOT
cat > TARGETS <<'EOI'
{ "script-hello":
  { "type": "generic"
  , "outs": ["script.sh"]
  , "cmds":
    [ "echo '#!/bin/sh' > script.sh"
    , "echo 'echo Hello World' >> script.sh"
    , "chmod 755 script.sh"
    ]
  }
, "generated-hello":
  { "type": "generic"
  , "outs": ["out-hello.txt"]
  , "deps": ["script-hello"]
  , "cmds": ["./script.sh > out-hello.txt"]
  }
, "script-morning":
  { "type": "generic"
  , "outs": ["script.sh"]
  , "cmds":
    [ "echo '#!/bin/sh' > script.sh"
    , "echo 'echo Good morning' >> script.sh"
    , "chmod 755 script.sh"
    ]
  }
, "generated-morning":
  { "type": "generic"
  , "outs": ["out-morning.txt"]
  , "deps": ["script-morning"]
  , "cmds": ["./script.sh > out-morning.txt"]
  }
, "ALL":
  {"type": "generic"
  , "deps": ["generated-hello", "generated-morning"]
  , "outs": ["out.txt"]
  , "cmds": ["cat out-*.txt > out.txt"]
  }
}
EOI


bin/tool-under-test install -o out --local-build-root .tool-root 2>&1
grep Hello out/out.txt
grep Good out/out.txt
