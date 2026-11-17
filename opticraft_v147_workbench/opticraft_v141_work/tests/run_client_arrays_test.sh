#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/opti_client_arrays_test"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/src/gl_compat_core.cpp" "$ROOT/src/client_arrays.cpp" "$ROOT/tests/client_arrays_test.cpp" -o "$OUT"
"$OUT"
