#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/src/switch_awt_host.cpp" "$ROOT/tests/switch_awt_host_test.cpp" -o "$TMP/test"
"$TMP/test"
