#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/src/java_runtime_bootstrap.cpp" \
  "$ROOT/src/classfile_inspector.cpp" \
  "$ROOT/src/class_code_parser.cpp" \
  "$ROOT/src/bytecode_verifier.cpp" \
  "$ROOT/tests/classfile_inspector_test.cpp" -lz -o "$TMP/classfile_test"
SOURCE="${OPTICRAFT_ZIP:-/mnt/data/OptiCraft JAVA LITE 1.2.5.zip}"
if [[ ! -f "$SOURCE" ]]; then echo "SKIP: original OptiCraft archive not supplied (set OPTICRAFT_ZIP)"; exit 0; fi
python3 - "$TMP/minecraft.jar" "$SOURCE" <<'PY'
import sys,zipfile
with zipfile.ZipFile(sys.argv[2]) as z:
    name=next(n for n in z.namelist() if n.endswith("/mcdata/bin/minecraft.jar"))
    with open(sys.argv[1],"wb") as out: out.write(z.read(name))
PY
"$TMP/classfile_test" "$TMP/minecraft.jar"
