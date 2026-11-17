#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JAVA_HOME="${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}"
JNI="$JAVA_HOME/include"
if [[ ! -f "$ROOT/tests/extracted/lwjgl.jar" ]]; then
  "$ROOT/tools/extract_test_lwjgl.sh" "$ROOT/tests/extracted/lwjgl.jar"
fi
mkdir -p "$ROOT/tests/build/unsealed"
(
  cd "$ROOT/tests/build/unsealed"
  jar xf "$ROOT/tests/extracted/lwjgl.jar"
  sed -i '/^Sealed: true/d' META-INF/MANIFEST.MF
  jar cfm "$ROOT/tests/build/lwjgl-unsealed-test.jar" META-INF/MANIFEST.MF -C "$ROOT/tests/build/unsealed" .
)
g++ -std=c++17 -O2 -Wall -Wextra -fPIC -shared \
  -I"$JNI" -I"$JNI/linux" "$ROOT/src/lwjgl_bufferutils_jni.cpp" \
  -o "$ROOT/tests/build/liblwjgl_switch_bridge_test.so"
javac -cp "$ROOT/tests/build/lwjgl-unsealed-test.jar" -d "$ROOT/tests/build" \
  "$ROOT/tests/java/OptiSysNativeTest.java"
java -Djava.awt.headless=true \
  -cp "$ROOT/tests/build:$ROOT/tests/build/lwjgl-unsealed-test.jar" \
  org.lwjgl.OptiSysNativeTest "$ROOT/tests/build/liblwjgl_switch_bridge_test.so"
