#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/opti_software_renderer_texture_test"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" \
 "$ROOT/src/gl_compat_core.cpp" "$ROOT/src/software_renderer.cpp" \
 "$ROOT/src/texture_store.cpp" "$ROOT/tests/software_renderer_texture_test.cpp" -o "$OUT"
"$OUT"
