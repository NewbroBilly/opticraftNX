#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
for script in \
  run_gl_compat_core_test.sh \
  run_gl_texture_api_test.sh \
  run_client_arrays_test.sh \
  run_legacy_gl_context_test.sh \
  run_software_renderer_texture_test.sh \
  run_framebuffer_blit_test.sh \
  run_lwjgl_gl11_bridge_test.sh; do
  echo "=== $script"
  bash "$ROOT/tests/$script"
done
