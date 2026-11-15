#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
OUT="\${TMPDIR:-/tmp}/opticraft-bytecode-smoke"
c++ -std=c++17 -Wall -Wextra -Werror -Wno-misleading-indentation -pedantic -I"$ROOT/runtime/jvm/include" \
  "$ROOT/runtime/jvm/src/opticraft_bytecode.cpp" \
  "$ROOT/runtime/jvm/tests/bytecode_smoke.cpp" -o "$OUT"
"$OUT"
echo "OptiCraft bytecode interpreter smoke: PASS"
