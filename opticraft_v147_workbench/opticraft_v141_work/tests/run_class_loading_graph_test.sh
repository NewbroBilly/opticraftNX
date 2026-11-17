#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SOURCE="${OPTICRAFT_ZIP:-/mnt/data/OptiCraft JAVA LITE 1.2.5.zip}"
if [[ ! -f "$SOURCE" ]]; then echo "SKIP: original OptiCraft archive not supplied (set OPTICRAFT_ZIP)"; exit 0; fi
python3 - "$TMP" "$SOURCE" <<'PY'
import sys, zipfile, os
out=sys.argv[1]
source=sys.argv[2]
with zipfile.ZipFile(source) as z:
    names=z.namelist()
    def extract(suffix, target):
        matches=[n for n in names if n.endswith(suffix)]
        if not matches: raise SystemExit('missing source entry: '+suffix)
        data=z.read(matches[0])
        with open(os.path.join(out,target),'wb') as f: f.write(data)
    extract('/mcdata/bin/minecraft.jar','minecraft.jar')
    extract('/mcdata/bin/lwjgl.jar','lwjgl.jar')
    extract('/mcdata/bin/lwjgl_util.jar','lwjgl_util.jar')
    extract('/mcdata/bin/jinput.jar','jinput.jar')
    extract('/mcdata/runtime/lib/rt.jar','rt.jar')
    with zipfile.ZipFile(os.path.join(out,'minecraft.jar')) as mc:
        class_bytes=mc.read('net/minecraft/client/Minecraft.class')
    with zipfile.ZipFile(os.path.join(out,'bad.jar'),'w',zipfile.ZIP_DEFLATED) as bad:
        bad.writestr('test/Mismatch.class',class_bytes)
PY
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -O2 -I"$ROOT/include" \
  "$ROOT/src/java_runtime_bootstrap.cpp" \
  "$ROOT/src/classfile_inspector.cpp" \
  "$ROOT/src/classpath_resolver.cpp" \
  "$ROOT/src/runtime_classpath.cpp" \
  "$ROOT/src/class_loading_graph.cpp" \
  "$ROOT/tests/class_loading_graph_test.cpp" -lz -o "$TMP/class_loading_graph_test"
"$TMP/class_loading_graph_test" \
  "$TMP/minecraft.jar" "$TMP/lwjgl.jar" "$TMP/lwjgl_util.jar" \
  "$TMP/jinput.jar" "$TMP/rt.jar" "$TMP/bad.jar"
