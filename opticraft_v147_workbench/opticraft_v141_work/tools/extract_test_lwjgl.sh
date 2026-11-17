#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-$ROOT/tests/extracted/lwjgl.jar}"
if [[ -f "$DEST" ]]; then exit 0; fi
SOURCE="${OPTICRAFT_ZIP:-/mnt/data/OptiCraft JAVA LITE 1.2.5.zip}"
if [[ ! -f "$SOURCE" ]]; then
  echo "Set OPTICRAFT_ZIP to your original OptiCraft Java 1.2.5 archive." >&2
  exit 2
fi
mkdir -p "$(dirname "$DEST")"
python3 - "$SOURCE" "$DEST" <<'PY'
import sys,zipfile
source,dest=sys.argv[1:]
with zipfile.ZipFile(source) as archive:
    matches=[n for n in archive.namelist() if n.endswith('/lwjgl.jar')]
    if len(matches)!=1:
        raise SystemExit('Expected exactly one original lwjgl.jar entry')
    with open(dest,'wb') as out: out.write(archive.read(matches[0]))
PY
echo "Extracted LWJGL test dependency to: $DEST"
