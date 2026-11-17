#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/optigl_texture_api_test"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" \
 "$ROOT/src/texture_store.cpp" "$ROOT/src/gl_texture_api.cpp" \
 "$ROOT/tests/gl_texture_api_test.cpp" -o "$OUT"
"$OUT"
