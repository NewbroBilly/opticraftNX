#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JAVA_HOME="${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}"
mkdir -p "$ROOT/tests/build/input_horizon"
g++ -std=c++17 -Wall -Wextra -I"$ROOT/tests/fake_switch" -I"$ROOT/include" \
  -fsyntax-only "$ROOT/src/switch_diagnostic_main.cpp"
g++ -std=c++17 -Wall -Wextra -D__SWITCH__ -fPIC -shared \
  -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" \
  -I"$ROOT/tests/fake_switch" -I"$ROOT/include" \
  "$ROOT/src/input_adapter.cpp" "$ROOT/src/switch_input_bridge.cpp" \
  "$ROOT/src/switch_input_bridge_jni.cpp" "$ROOT/src/switch_diagnostic_main.cpp" \
  "$ROOT/src/framebuffer_blit.cpp" \
  -o "$ROOT/tests/build/input_horizon/libswitchinput_horizon_mock.so"
javac -d "$ROOT/tests/build/input_horizon" \
  "$ROOT/java/org/lwjgl/switchport/SwitchInputBridge.java" \
  "$ROOT/tests/java/org/lwjgl/switchport/SwitchInputBridgeTest.java"
java -cp "$ROOT/tests/build/input_horizon" \
  org.lwjgl.switchport.SwitchInputBridgeTest \
  "$ROOT/tests/build/input_horizon/libswitchinput_horizon_mock.so"
echo "PASS: libnx backend + JNI polling hook link and run against mock libnx"
