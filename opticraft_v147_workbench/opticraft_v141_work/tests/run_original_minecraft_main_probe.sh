#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
JAR="${1:-}"
if [[ -z "$JAR" ]]; then echo "usage: $0 /path/to/minecraft.jar [instruction_budget]" >&2; exit 2; fi
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
 "$ROOT/src/java_runtime_bootstrap.cpp" "$ROOT/src/class_code_parser.cpp" \
 "$ROOT/src/jvm_runtime.cpp" "$ROOT/src/jvm_class_runtime.cpp" \
 "$ROOT/src/jvm_exception_runtime.cpp" "$ROOT/src/jvm_object_interpreter.cpp" \
 "$ROOT/src/gl_compat_core.cpp" "$ROOT/src/client_arrays.cpp" "$ROOT/src/software_renderer.cpp" \
 "$ROOT/src/texture_store.cpp" "$ROOT/src/gl_texture_api.cpp" "$ROOT/src/legacy_gl_context.cpp" \
 "$ROOT/src/lwjgl_gl11_bridge.cpp" "$ROOT/tools/run_original_minecraft_main_probe.cpp" -lz -lpng -o "$TMP/probe"
"$TMP/probe" "$JAR" "${2:-1000000}" "${3:-}"
