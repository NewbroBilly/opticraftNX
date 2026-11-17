#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/ResumeObjectFixture.java" <<'JAVA'
public class ResumeObjectFixture {
  public int value;
  public static int counter;
  public static final int MAGIC = 73;
  public static int initialized = 0;
  static { initialized = 74; }
  public int add(int x) { return value + x; }
  public static int plus(int a, int b) { return a + b; }
  public static int twice(int x) { return plus(x, x); }
  public static int arrayWork() { int[] a = new int[2]; a[0] = 19; a[1] = 23; return a[0] + a[1]; }
  public static int staticWork() { counter = 41; return counter + 1; }
  public static int catchWork(Object e) { try { throw (ResumeObjectFixture.MyException)e; } catch (ResumeObjectFixture.MyException ex) { return 42; } }
  public static int classInitWork() { return MAGIC + initialized; }
  public static int divide(int x) { return 100 / x; }
  public static int catchArithmetic(int x) { try { return divide(x); } catch (ArithmeticException ex) { return 53; } }
  public static int catchNull(Object[] a) { try { return a.length; } catch (NullPointerException ex) { return 54; } }
  public static int catchStore(Object[] a, Object value) { try { a[0] = value; } catch (ArrayStoreException ex) { return 55; } return 0; }
  public static int catchBounds(int[] a) { try { return a[4]; } catch (ArrayIndexOutOfBoundsException ex) { return 56; } }
  public static int catchCast(Object value) { try { return ((String)value).length(); } catch (ClassCastException ex) { return 57; } }
  static class MyException extends RuntimeException {}
}
JAVA
"${JAVAC:-javac}" -source 8 -target 8 -g:none -d "$TMP" "$TMP/ResumeObjectFixture.java" 2>"$TMP/javac.log"
"${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
 "$ROOT/src/class_code_parser.cpp" "$ROOT/src/jvm_runtime.cpp" "$ROOT/src/jvm_class_runtime.cpp" \
 "$ROOT/src/jvm_resumable_interpreter.cpp" "$ROOT/src/jvm_resumable_object_interpreter.cpp" "$ROOT/src/java_thread_scheduler.cpp" "$ROOT/tests/jvm_resumable_object_interpreter_test.cpp" -o "$TMP/test"
"$TMP/test" "$TMP/ResumeObjectFixture.class"
