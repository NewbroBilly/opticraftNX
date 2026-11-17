#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/opticraft_java_thread_scheduler_test"
c++ -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/src/java_thread_scheduler.cpp" "$ROOT/tests/java_thread_scheduler_test.cpp" -o "$OUT"
"$OUT"
echo "Java thread scheduler test: PASS"
