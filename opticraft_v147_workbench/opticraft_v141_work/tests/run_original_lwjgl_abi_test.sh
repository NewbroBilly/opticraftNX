#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JAVA_HOME="${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}"
ORIGINAL_ZIP="${OPTICRAFT_ZIP:-/mnt/data/OptiCraft JAVA LITE 1.2.5.zip}"
if [[ ! -f "$ORIGINAL_ZIP" ]]; then
  echo "SKIP: original OptiCraft archive not supplied (set OPTICRAFT_ZIP)"
  exit 0
fi
mkdir -p "$ROOT/tests/extracted"
python3 - "$ORIGINAL_ZIP" "$ROOT/tests/extracted/lwjgl.jar" <<'PY'
import sys, zipfile
src, dst = sys.argv[1:]
with zipfile.ZipFile(src) as z:
    name = next(n for n in z.namelist() if n.endswith("/lwjgl.jar"))
    open(dst, "wb").write(z.read(name))
PY
g++ -std=c++17 -Wall -Wextra -Werror -fPIC -shared \
  -DOPTI_JNI_CLASS='"org/lwjgl/BufferUtils"' \
  -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" \
  "$ROOT/src/lwjgl_bufferutils_jni.cpp" \
  -o "$ROOT/tests/liblwjgl_bufferutils_host.so"
javac -cp "$ROOT/tests/extracted/lwjgl.jar" \
  "$ROOT/tests/java/OptiRealLwjglBufferUtilsTest.java"
java -Dopti.bridge="$ROOT/tests/liblwjgl_bufferutils_host.so" \
  -cp "$ROOT/tests/extracted/lwjgl.jar:$ROOT/tests/java" \
  OptiRealLwjglBufferUtilsTest
