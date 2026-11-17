# OptiCraft Switch v133 — checkpoint/restore контекста Java-потока

## Что изменено

- `JavaThreadExecutionRuntime::Checkpoint` теперь фиксирует ID потока, полный снимок активных `JvmExecutionFrame` и число scheduler steps.
- Добавлены `checkpoint(id, out)` и `restore(checkpoint)` с проверкой существования контекста и предельной глубины стека.
- Добавлены тесты сохранения/восстановления PC, locals/operand stack через frame snapshot и счётчика шагов, а также отказа для неизвестного ThreadId.
- Сохранена независимость контекстов разных потоков.

## Проверки

Успешно выполнены:
- `run_java_thread_scheduler_test.sh`
- `run_java_thread_execution_runtime_test.sh`
- `run_jvm_object_interpreter_test.sh`
- `run_jvm_execution_test.sh`

Это host-тесты C++-прототипа. Они не подтверждают запуск NRO на Nintendo Switch.

## Ограничения

Checkpoint сохраняет диагностические кадры из `JvmExecutionFrameStack`, но текущий `ObjectBytecodeInterpreter::execute` остаётся рекурсивным и синхронным. Он не умеет остановить выполнение посреди метода и продолжить с того же PC. Поэтому checkpoint/restore контекста пока не является полноценным continuation Java bytecode. `Thread.start()` и scheduler пока не исполняют реальный Java call stack независимо от вызывающего потока.

Для настоящего resumable interpreter необходимо вынести локальные переменные, operand stack, PC, method/class reference, exception state и continuation вызывающего метода в долговременный объект исполнения; затем переписать цикл интерпретации на ограниченный `step()` без рекурсивного выполнения вложенных методов.
