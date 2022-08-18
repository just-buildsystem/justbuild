#!/bin/sh
set -e

TOOL=$(realpath ./bin/tool-under-test)
mkdir -p .root
BUILDROOT=$(realpath .root)
mkdir -p out
OUTDIR=$(realpath out)


mkdir src
cd src
touch ROOT

cat > repos.json <<'EOF'
{"repositories": {"": {"target_file_name": "TARGETS.local"}}}
EOF
export CONF=$(realpath repos.json)

cat > TARGETS.local <<'EOF'
{"a": {"type": "file_gen", "name": "a.txt", "data": "A-top-level"}}
EOF

echo
echo === Default target ===
${TOOL} build -C $CONF --local-build-root ${BUILDROOT} -Pa.txt | grep top-level


echo
echo === top-level module found ===

mkdir foo
cd foo
cat > TARGETS <<'EOF'
{"a": {"type": "file_gen", "name": "a.txt", "data": "WRONG"}}
EOF
${TOOL} build -C $CONF --local-build-root ${BUILDROOT} -Pa.txt | grep top-level

echo === correct root referece ===

cat > TARGETS.local <<'EOF'
{"a": {"type": "file_gen", "name": "b.txt", "data": "A-local"}
, "": {"type": "install", "deps": ["a", ["", "a"]]}
}
EOF

${TOOL} install -C $CONF --local-build-root ${BUILDROOT} -o ${OUTDIR}/top-ref 2>&1
echo
grep top-level ${OUTDIR}/top-ref/a.txt
grep local ${OUTDIR}/top-ref/b.txt

echo
echo === DONE ===
