#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
  "$ROOT/src/java_thread_scheduler.cpp" "$ROOT/src/class_code_parser.cpp" \
  "$ROOT/src/jvm_runtime.cpp" "$ROOT/src/jvm_class_runtime.cpp" \
  "$ROOT/src/jvm_resumable_interpreter.cpp" "$ROOT/src/jvm_resumable_object_interpreter.cpp" \
  "$ROOT/tests/java_thread_execution_runtime_test.cpp" -o "$TMP/test"
cat > "$TMP/ThreadFixture.java" <<'JAVA'
public class ThreadFixture {
  public static int sum(int n) { int total=0; for(int i=1;i<=n;i++) total+=i; return total; }
  public static int factorial(int n) { if(n<=1)return 1; return n*factorial(n-1); }
}
JAVA
"${JAVAC:-javac}" -source 8 -target 8 -g:none -d "$TMP" "$TMP/ThreadFixture.java" 2>"$TMP/javac.log"
"$TMP/test" "$TMP/ThreadFixture.class"
