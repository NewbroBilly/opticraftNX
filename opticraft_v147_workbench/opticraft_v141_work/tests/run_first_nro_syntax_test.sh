#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -Wall -Wextra -Werror \
  -I"$ROOT/tests/fake_switch_nro" \
  -fsyntax-only "$ROOT/switch_nro/main.cpp"
echo "PASS: first-NRO entry point C++ syntax (host mock only)"
