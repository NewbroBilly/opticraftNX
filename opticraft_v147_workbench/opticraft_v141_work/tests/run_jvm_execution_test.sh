#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/VmFixture.java" <<'JAVA'
public class VmFixture {
  public int instanceValue;
  public static int staticValue;
  public static int sum(int n) {
    int total = 0;
    for (int i = 1; i <= n; i++) total += i;
    return total;
  }
  public static int branch(int value) {
    if (value < 0) return -value;
    return value + 2;
  }
  public static int choose(int x) {
    switch (x) { case 1: return 20; case 2: return 30; case 3: return 40; default: return -5; }
  }
  public static int constant() { return 1234567; }
  public static int sparseSwitch(int x) {
    switch (x) { case 1: return 11; case 100: return 12; case 1000: return 13; default: return -1; }
  }
  public static int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
  }
  public static int divide(int a, int b) { return a / b; }
  public static int guarded(int x) {
    try { return 100 / x; }
    catch (ArithmeticException ex) { return -1; }
  }
}
JAVA
python3 - "$TMP/VmFixture.java" <<'PY'
import sys
p=sys.argv[1]
s=open(p).read()
pos=s.rfind("}")
decls="\n".join("    int v%d = %d;"%(i,i) for i in range(260))
s=s[:pos]+"  public static int wideLocal() {\n"+decls+"\n    return v259;\n  }\n"+s[pos:]
open(p,"w").write(s)
PY
"${JAVAC:-javac}" -source 8 -target 8 -g:none -d "$TMP" "$TMP/VmFixture.java" 2>"$TMP/javac.log"
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
 "$ROOT/src/class_code_parser.cpp" "$ROOT/src/jvm_interpreter.cpp" \
 "$ROOT/tests/jvm_execution_test.cpp" -o "$TMP/test"
"$TMP/test" "$TMP/VmFixture.class"
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
 "$ROOT/src/class_code_parser.cpp" "$ROOT/src/jvm_interpreter.cpp" \
 "$ROOT/tools/run_java_int_method.cpp" -o "$TMP/run_java_int_method"
"$TMP/run_java_int_method" "$TMP/VmFixture.class" sum "(I)I" 100 | grep -q '^5050$' 
