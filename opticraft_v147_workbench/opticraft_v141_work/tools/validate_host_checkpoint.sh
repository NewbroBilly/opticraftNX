#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JAR="${1:-}"
if [[ -z "$JAR" ]]; then echo "usage: $0 /path/to/minecraft.jar" >&2; exit 2; fi
cd "$ROOT"
for script in \
  run_jvm_runtime_test.sh \
  run_jvm_class_runtime_test.sh \
  run_jvm_exception_runtime_test.sh \
  run_jvm_execution_test.sh \
  run_jvm_object_interpreter_test.sh \
  run_jvm_java_lang_intrinsics_test.sh \
  run_bytecode_verifier_test.sh \
  run_class_linker_test.sh \
  run_classfile_inspector_test.sh \
  run_awt_compatibility_test.sh \
  run_switch_awt_host_test.sh; do
  echo "=== $script"
  timeout 240 bash "$ROOT/tests/$script"
done
echo "=== original Minecraft main probe"
timeout 240 bash "$ROOT/tests/run_original_minecraft_main_probe.sh" "$JAR"
