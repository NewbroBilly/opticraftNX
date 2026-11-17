#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JAVA_HOME="${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}"
mkdir -p "$ROOT/tests/build/input"
g++ -std=c++17 -O2 -Wall -Wextra -fPIC -shared \
  -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" \
  -I"$ROOT/include" \
  "$ROOT/src/input_adapter.cpp" "$ROOT/src/switch_input_bridge.cpp" \
  "$ROOT/src/switch_input_bridge_jni.cpp" \
  -o "$ROOT/tests/build/input/libswitchinput_test.so"
javac -d "$ROOT/tests/build/input" \
  "$ROOT/java/org/lwjgl/switchport/SwitchInputBridge.java" \
  "$ROOT/tests/java/org/lwjgl/switchport/SwitchInputBridgeTest.java"
java -cp "$ROOT/tests/build/input" org.lwjgl.switchport.SwitchInputBridgeTest \
  "$ROOT/tests/build/input/libswitchinput_test.so"
