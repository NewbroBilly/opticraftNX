#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)/opti_inspect_minecraft_jar"
TMP="$(mktemp -d)/opti_original_minecraft_jar"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/src/java_runtime_bootstrap.cpp" "$ROOT/tools/inspect_minecraft_jar.cpp" -lz -o "$OUT"
SOURCE="${OPTICRAFT_ZIP:-/mnt/data/OptiCraft JAVA LITE 1.2.5.zip}"
if [[ ! -f "$SOURCE" ]]; then echo "SKIP: original OptiCraft archive not supplied (set OPTICRAFT_ZIP)"; exit 0; fi
python3 - "$TMP" "$SOURCE" <<'PY'
import sys,zipfile
outer=sys.argv[2]
with zipfile.ZipFile(outer) as z:
    name=next(n for n in z.namelist() if n.endswith("/mcdata/bin/minecraft.jar"))
    open(sys.argv[1],"wb").write(z.read(name))
PY
"$OUT" "$TMP"
EXTRACTED="${TMP}.Minecraft.class"
"$OUT" "$TMP" --extract net/minecraft/client/Minecraft.class "$EXTRACTED"
python3 - "$EXTRACTED" <<'PYCLASS'
import sys
p=sys.argv[1]
data=open(p,"rb").read()
assert data[:4] == bytes.fromhex("cafebabe"), "invalid class magic"
major=int.from_bytes(data[6:8],"big")
assert major == 49, f"expected Java 5 classfile major 49, got {major}"
print(f"original Minecraft main class extracted; bytes={len(data)} major={major}")
PYCLASS
