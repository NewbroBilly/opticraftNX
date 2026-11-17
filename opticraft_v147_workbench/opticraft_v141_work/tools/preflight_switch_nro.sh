#!/usr/bin/env bash
set -euo pipefail
: "${DEVKITPRO:?DEVKITPRO is not set}"
for p in \
  "$DEVKITPRO/libnx/switch_rules" \
  "$DEVKITPRO/libnx/switch.specs" \
  "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++" \
  "$DEVKITPRO/tools/bin/elf2nro" \
  "$DEVKITPRO/tools/bin/nacptool"; do
  if [[ ! -e "$p" ]]; then
    echo "MISSING: $p" >&2
    exit 2
  fi
done
command -v make >/dev/null || { echo "MISSING: make" >&2; exit 2; }
echo "PASS: devkitPro NRO toolchain files found"
