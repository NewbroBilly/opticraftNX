#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SOURCE="${OPTICRAFT_ZIP:-/mnt/data/OptiCraft JAVA LITE 1.2.5.zip}"
if [[ ! -f "$SOURCE" ]]; then echo "SKIP: original OptiCraft archive not supplied (set OPTICRAFT_ZIP)"; exit 0; fi
python3 - "$TMP" "$SOURCE" <<'PY'
import sys,zipfile,os
out=sys.argv[1]
with zipfile.ZipFile(sys.argv[2]) as z:
    names=z.namelist()
    for suffix,target in [
        ('/mcdata/bin/minecraft.jar','minecraft.jar'),
        ('/mcdata/bin/lwjgl.jar','lwjgl.jar'),
        ('/mcdata/bin/lwjgl_util.jar','lwjgl_util.jar'),
        ('/mcdata/bin/jinput.jar','jinput.jar'),
        ('/mcdata/runtime/lib/rt.jar','rt.jar')]:
        name=next((n for n in names if n.endswith(suffix)),None)
        if name is None: raise SystemExit('missing '+suffix)
        with open(os.path.join(out,target),'wb') as f: f.write(z.read(name))
PY
"$ROOT/tools/build_class_dependency_scan.sh" "$TMP/class_dependency_scan"
cd "$TMP"
./class_dependency_scan net.minecraft.client.Minecraft 256 \
  --bootstrap=rt.jar --app=minecraft.jar --app=lwjgl.jar \
  --app=lwjgl_util.jar --app=jinput.jar --nodes=nodes.csv --edges=edges.csv
 grep -q '^class,state,source,major,bytes,super_class,reference_count,error$' nodes.csv
 grep -q 'net.minecraft.client.Minecraft' nodes.csv
 grep -q '^from_class,to_class$' edges.csv
 echo 'PASS: standalone dependency-scan CLI and CSV output'
