#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/src/class_linker.cpp" "$ROOT/tests/class_linker_test.cpp" -o "$TMP/test"
"$TMP/test"
