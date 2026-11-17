#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)/opti_java_runtime_bootstrap_test"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/src/java_runtime_bootstrap.cpp" "$ROOT/tests/java_runtime_bootstrap_test.cpp" -lz -o "$OUT"
"$OUT"
