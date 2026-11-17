#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/opti_lwjgl_gl11_bridge_test"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" \
 "$ROOT/src/gl_compat_core.cpp" "$ROOT/src/client_arrays.cpp" \
 "$ROOT/src/software_renderer.cpp" "$ROOT/src/texture_store.cpp" \
 "$ROOT/src/gl_texture_api.cpp" "$ROOT/src/legacy_gl_context.cpp" \
 "$ROOT/src/lwjgl_gl11_bridge.cpp" "$ROOT/tests/lwjgl_gl11_bridge_test.cpp" -o "$OUT"
"$OUT"
