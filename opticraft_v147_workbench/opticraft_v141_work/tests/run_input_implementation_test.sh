#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JAVA_HOME="${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}"
LWJGL_JAR="${LWJGL_JAR:-$ROOT/tests/extracted/lwjgl.jar}"
if [[ ! -f "$LWJGL_JAR" ]]; then
  "$ROOT/tools/extract_test_lwjgl.sh" "$LWJGL_JAR"
fi
mkdir -p "$ROOT/tests/build/input_impl"
g++ -std=c++17 -O2 -Wall -Wextra -fPIC -shared \
  -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" -I"$ROOT/include" \
  "$ROOT/src/input_adapter.cpp" "$ROOT/src/switch_input_bridge.cpp" \
  "$ROOT/src/switch_input_bridge_jni.cpp" \
  -o "$ROOT/tests/build/input_impl/libswitchinput_test.so"
javac -cp "$LWJGL_JAR" -d "$ROOT/tests/build/input_impl" \
  "$ROOT/java/org/lwjgl/switchport/SwitchInputBridge.java" \
  "$ROOT/java/org/lwjgl/switchport/SwitchInputImplementation.java" \
  "$ROOT/java/org/lwjgl/switchport/SwitchInputBootstrap.java" \
  "$ROOT/java/org/lwjgl/switchport/SwitchMinecraftLauncher.java" \
  "$ROOT/tests/java/net/minecraft/client/Minecraft.java" \
  "$ROOT/tests/java/org/lwjgl/switchport/SwitchMinecraftLauncherTest.java" \
  "$ROOT/tests/java/org/lwjgl/switchport/SwitchInputImplementationTest.java"
java -cp "$ROOT/tests/build/input_impl:$LWJGL_JAR" \
  org.lwjgl.switchport.SwitchInputImplementationTest \
  "$ROOT/tests/build/input_impl/libswitchinput_test.so"
java -cp "$ROOT/tests/build/input_impl:$LWJGL_JAR" \
  org.lwjgl.switchport.SwitchMinecraftLauncherTest
