#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/tests/build/glcompat"
g++ -std=c++17 -O2 -Wall -Wextra -I"$ROOT/include" \
  "$ROOT/src/gl_compat_core.cpp" "$ROOT/src/software_renderer.cpp" \
  "$ROOT/src/texture_store.cpp" \
  "$ROOT/tests/gl_compat_core_test.cpp" \
  -o "$ROOT/tests/build/glcompat/gl_compat_core_test"
"$ROOT/tests/build/glcompat/gl_compat_core_test"
