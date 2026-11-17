#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/opti_legacy_gl_context_test"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" \
 "$ROOT/src/gl_compat_core.cpp" "$ROOT/src/client_arrays.cpp" "$ROOT/src/software_renderer.cpp" \
 "$ROOT/src/texture_store.cpp" "$ROOT/src/gl_texture_api.cpp" "$ROOT/src/legacy_gl_context.cpp" \
 "$ROOT/tests/legacy_gl_context_test.cpp" -o "$OUT"
"$OUT"
