#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/class_dependency_scan}"
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/src/java_runtime_bootstrap.cpp" \
  "$ROOT/src/classfile_inspector.cpp" \
  "$ROOT/src/classpath_resolver.cpp" \
  "$ROOT/src/runtime_classpath.cpp" \
  "$ROOT/src/class_loading_graph.cpp" \
  "$ROOT/tools/class_dependency_scan.cpp" -lz -o "$OUT"
echo "Built: $OUT"
