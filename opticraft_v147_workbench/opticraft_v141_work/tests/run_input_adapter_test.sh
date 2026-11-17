#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/tests/build/input_adapter"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/src/input_adapter.cpp" "$ROOT/tests/input_adapter_test.cpp" \
  -o "$ROOT/tests/build/input_adapter/input_adapter_test"
"$ROOT/tests/build/input_adapter/input_adapter_test"
echo "PASS: Switch controller mapping and edge-triggered events"
