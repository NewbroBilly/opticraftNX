#!/usr/bin/env sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
OUT="$ROOT/tests/texture_store_test"
g++ -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/src/texture_store.cpp" "$ROOT/tests/texture_store_test.cpp" -o "$OUT"
"$OUT"
echo "texture_store_test: PASS"
