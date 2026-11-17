#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" \
 "$ROOT/src/java_runtime_bootstrap.cpp" "$ROOT/src/classfile_inspector.cpp" \
 "$ROOT/src/classpath_resolver.cpp" "$ROOT/src/runtime_classpath.cpp" \
 "$ROOT/tests/runtime_classpath_test.cpp" -lz -o "$TMP/runtime_classpath_test"
SOURCE="${OPTICRAFT_ZIP:-/mnt/data/OptiCraft JAVA LITE 1.2.5.zip}"
if [[ ! -f "$SOURCE" ]]; then echo "SKIP: original OptiCraft archive not supplied (set OPTICRAFT_ZIP)"; exit 0; fi
python3 - "$TMP/minecraft.jar" "$SOURCE" <<'PY'
import sys,zipfile
with zipfile.ZipFile(sys.argv[2]) as z:
 n=next(n for n in z.namelist() if n.endswith("/mcdata/bin/minecraft.jar"))
 open(sys.argv[1],"wb").write(z.read(n))
PY
"$TMP/runtime_classpath_test" "$TMP/minecraft.jar"
