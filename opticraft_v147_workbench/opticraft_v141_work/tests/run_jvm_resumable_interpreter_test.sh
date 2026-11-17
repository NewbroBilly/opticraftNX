#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/ResumeFixture.java" <<'JAVA'
public class ResumeFixture {
  public static int sum(int n) { int total=0; for(int i=1;i<=n;i++) total+=i; return total; }
  public static int branch(int x) { if(x<0)return -x; return x+2; }
  public static int factorial(int n) { if(n<=1)return 1; return n*factorial(n-1); }
}
JAVA
"${JAVAC:-javac}" -source 8 -target 8 -g:none -d "$TMP" "$TMP/ResumeFixture.java" 2>"$TMP/javac.log"
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
 "$ROOT/src/class_code_parser.cpp" "$ROOT/src/jvm_resumable_interpreter.cpp" \
 "$ROOT/tests/jvm_resumable_interpreter_test.cpp" -o "$TMP/test"
"$TMP/test" "$TMP/ResumeFixture.class"
