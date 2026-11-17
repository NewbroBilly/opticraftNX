#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/opti_framebuffer_blit_test"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/src/framebuffer_blit.cpp" "$ROOT/tests/framebuffer_blit_test.cpp" -o "$OUT"
"$OUT"
