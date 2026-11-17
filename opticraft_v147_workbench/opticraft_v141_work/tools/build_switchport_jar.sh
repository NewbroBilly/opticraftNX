#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${LWJGL_JAR:?Set LWJGL_JAR to the original LWJGL 2.9.4 lwjgl.jar}"
: "${JAVAC:=javac}"
: "${JAR:=jar}"
OUT="${1:-$ROOT/build/switchport.jar}"
WORK="${TMPDIR:-/tmp}/opti_switchport_classes"
rm -rf "$WORK"
mkdir -p "$WORK" "$(dirname "$OUT")"
"$JAVAC" -source 1.8 -target 1.8 -Xlint:-options -cp "$LWJGL_JAR" -d "$WORK" \
  "$ROOT/java/org/lwjgl/switchport/SwitchInputBridge.java" \
  "$ROOT/java/org/lwjgl/switchport/SwitchInputImplementation.java" \
  "$ROOT/java/org/lwjgl/switchport/SwitchInputBootstrap.java" \
  "$ROOT/java/org/lwjgl/switchport/SwitchMinecraftLauncher.java"
"$JAR" cf "$OUT" -C "$WORK" .
python3 - "$OUT" <<'PY'
import sys,zipfile,struct
p=sys.argv[1]
with zipfile.ZipFile(p) as z:
    classes=[n for n in z.namelist() if n.endswith(".class")]
    if not classes: raise SystemExit("no class files in switchport.jar")
    versions=[]
    for n in classes:
        d=z.read(n)
        if d[:4]!=b"\xca\xfe\xba\xbe": raise SystemExit("invalid class file: "+n)
        versions.append(struct.unpack(">H",d[6:8])[0])
    if max(versions)>52:
        raise SystemExit("Switch adapter bytecode is newer than Java 8 (major 52)")
print("switchport.jar verified; classes=%d max_class_major=%d"%(len(classes),max(versions)))
PY
