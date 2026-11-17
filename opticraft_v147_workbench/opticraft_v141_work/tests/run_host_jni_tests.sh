#!/usr/bin/env bash
set -euo pipefail
JAVA_HOME="${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/tests"
g++ -std=c++17 -Wall -Wextra -Werror -fPIC -shared \
  -DOPTI_JNI_CLASS='"OptiBufferBridgeTest"' -DOPTI_JNI_TEST \
  -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" \
  "$ROOT/src/lwjgl_bufferutils_jni.cpp" \
  -o "$OUT/libopti_buffer_test.so"
javac "$OUT/java/OptiBufferBridgeTest.java"
java -Dopti.bridge="$OUT/libopti_buffer_test.so" \
  -cp "$OUT/java" OptiBufferBridgeTest
