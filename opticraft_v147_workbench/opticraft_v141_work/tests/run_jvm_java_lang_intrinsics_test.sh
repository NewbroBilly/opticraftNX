#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
 "$ROOT/src/jvm_runtime.cpp" "$ROOT/src/jvm_class_runtime.cpp" "$ROOT/src/jvm_exception_runtime.cpp" \
 "$ROOT/src/jvm_object_interpreter.cpp" \
  "$ROOT/src/gl_compat_core.cpp" "$ROOT/src/client_arrays.cpp" \
  "$ROOT/src/software_renderer.cpp" "$ROOT/src/texture_store.cpp" \
  "$ROOT/src/gl_texture_api.cpp" "$ROOT/src/legacy_gl_context.cpp" \
  "$ROOT/src/lwjgl_gl11_bridge.cpp" "$ROOT/tests/jvm_java_lang_intrinsics_test.cpp" -lz -lpng -o "$TMP/test"
"$TMP/test"
