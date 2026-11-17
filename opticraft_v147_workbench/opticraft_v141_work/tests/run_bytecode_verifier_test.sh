#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/src/bytecode_verifier.cpp" "$ROOT/tests/bytecode_verifier_test.cpp" -o "$TMP/test"
"$TMP/test"
