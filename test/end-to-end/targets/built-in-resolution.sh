#!/bin/sh
set -e

TOOL=$(realpath ./bin/tool-under-test)
mkdir -p .root
BUILDROOT=$(realpath .root)
mkdir -p out
OUTDIR=$(realpath out)

mkdir -p  src/some/module
touch src/ROOT
cd src/some/module
cat > RULES <<'EOI'
{ "install":
  { "expression":
    { "type": "RESULT"
    , "artifacts":
      { "type": "singleton_map"
      , "key": "some internal path"
      , "value": {"type": "BLOB", "data": "FROM USER-DEFINED INSTALL"}
      }
    }
  }
}
EOI
cat > TARGETS <<'EOI'
{ "user": {"type": ["./", ".", "install"]}
, "built-in": {"type": "install", "files": {"the/public/path": "user"}}
}
EOI

echo
${TOOL} install built-in --local-build-root ${BUILDROOT} -o ${OUTDIR} 2>&1
echo
grep 'FROM USER-DEFINED INSTALL' ${OUTDIR}/the/public/path
echo

echo DONE
