# OptiCraft Switch v132 — Thread Execution Contexts & Frame Checkpoints

## Большой блок

- `JvmExecutionFrameStack` получил `restore()` для восстановления ранее снятого checkpoint и `detach()` для передачи стека между владельцами контекста. Ограничение глубины восстановления — 256 кадров.
- `JavaThreadScheduler` получил состояние `Parked`, операции `park()` / `resume()` и чтение/сброс interrupt-флага через `interrupted(id, clear)`.
- Парковка сохраняется после возврата cooperative step callback; `is_alive()` учитывает Parked как живой поток.
- Добавлен `JavaThreadExecutionRuntime`: контекст с собственным `JvmExecutionFrameStack` на каждый scheduler thread, callback получает свой контекст, есть API старта, планирования, парковки, пробуждения и interrupt.
- Добавлен тест независимых контекстов двух потоков, изоляции их frame stacks, park/resume, завершения и frame checkpoint restore/detach.
- Новый тест включён в integrated runtime suite.

## Проверки

PASS:
- `run_java_thread_execution_runtime_test.sh`
- `run_java_thread_scheduler_test.sh`
- `run_jvm_execution_frames_test.sh`
- `run_jvm_object_interpreter_test.sh`
- В integrated suite до остановки: JVM runtime, class runtime, exception runtime, execution, object interpreter и integrated invocation прошли.

Не завершено:
- Integrated suite остановлен по тайм-лимиту во время компиляции `jvm_java_lang_intrinsics_test`; последующие suite tests не запускались.
- NRO не собирался; Nintendo Switch не тестировался.

## Техническая граница

v132 создаёт независимые контейнеры frame checkpoint и cooperative scheduler contexts, но **не подключает resumable interpreter к Java bytecode**. `ObjectBytecodeInterpreter` всё ещё выполняет метод синхронным рекурсивным вызовом. Scheduler callbacks должны сами выполнять ограниченную порцию работы и yield; автоматического сохранения/восстановления PC/locals/operand stack внутри interpreter пока нет. `sleep()`/`join()` и настоящая конкурентность Java не объявляются реализованными.

Следующий критический этап — выделить интерпретатор в `begin/step/resume` state machine и передавать реальные Java frames через `JavaThreadExecutionRuntime`.
