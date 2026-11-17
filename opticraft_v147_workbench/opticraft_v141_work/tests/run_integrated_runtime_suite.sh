#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SCRIPTS=(
  run_jvm_runtime_test.sh
  run_jvm_class_runtime_test.sh
  run_jvm_exception_runtime_test.sh
  run_jvm_execution_test.sh
  run_jvm_resumable_interpreter_test.sh
  run_jvm_object_interpreter_test.sh
  run_jvm_resumable_object_interpreter_test.sh
  run_jvm_integrated_invocation_test.sh
  run_jvm_java_lang_intrinsics_test.sh
  run_bytecode_verifier_test.sh
  run_class_linker_test.sh
  run_classfile_inspector_test.sh
  run_awt_compatibility_test.sh
  run_java_thread_scheduler_test.sh
  run_jvm_execution_frames_test.sh
  run_java_thread_execution_runtime_test.sh
)
for script in "${SCRIPTS[@]}"; do
  echo "=== $script"
  bash "$ROOT/tests/$script"
done
