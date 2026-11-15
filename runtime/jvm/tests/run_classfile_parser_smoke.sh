#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
OUT="${TMPDIR:-/tmp}/opticraft-classfile-parser-smoke"
c++ -std=c++17 -Wall -Wextra -Werror -Wno-misleading-indentation -pedantic -I"$ROOT/runtime/jvm/include" \
 "$ROOT/runtime/jvm/src/opticraft_classfile.cpp" \
 "$ROOT/runtime/jvm/src/opticraft_bytecode.cpp" \
 "$ROOT/runtime/jvm/src/opticraft_method_executor.cpp" \
 "$ROOT/runtime/jvm/src/opticraft_jar_classloader.cpp" \
 "$ROOT/runtime/jvm/tests/classfile_parser_smoke.cpp" -lz -o "$OUT"
"$OUT"
echo "OptiCraft classfile parser smoke: PASS"
