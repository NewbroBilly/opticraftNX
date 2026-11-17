# OptiCraft Switch v131 — JVM Execution Frame State Foundation

## Крупный блок изменений

1. Добавлена структура `JvmExecutionFrame`: имя класса/метода, дескриптор, bytecode PC, локальные переменные, operand stack, receiver, pending exception и счётчик обновлений.
2. Добавлен `JvmExecutionFrameStack` с push/pop, текущим стеком, snapshot и журналом завершённых кадров.
3. `ObjectBytecodeInterpreter` теперь регистрирует кадр при входе в метод, обновляет его на границах bytecode-инструкций и перед передачей управления вызываемому методу, а затем сохраняет завершённый кадр.
4. Состояние caller остаётся в стеке во время рекурсивного вызова callee; это даёт основу для диагностики цепочек вызовов и будущего переноса интерпретатора на resumable execution.
5. Добавлены отдельный тест frame snapshot и проверки того, что реальный ObjectBytecodeInterpreter записывает завершённый кадр.
6. Тест включён в `run_integrated_runtime_suite.sh`.

## Проверки, выполненные на host

PASS:
- `run_jvm_runtime_test.sh`
- `run_jvm_class_runtime_test.sh`
- `run_jvm_exception_runtime_test.sh`
- `run_jvm_execution_test.sh`
- `run_jvm_object_interpreter_test.sh` (включая проверку сохранённого кадра)
- `run_java_thread_scheduler_test.sh`
- `run_jvm_execution_frames_test.sh`
- `run_bytecode_verifier_test.sh`
- `run_class_linker_test.sh`
- `run_awt_compatibility_test.sh`

Не подтверждено:
- `run_jvm_java_lang_intrinsics_test.sh` — завис/не завершился в лимит времени при запуске последовательности проверок; не засчитывается как PASS.
- `run_classfile_inspector_test.sh` — не запустился: отсутствует внешний архив `/mnt/data/OptiCraft JAVA LITE 1.2.5.zip`.
- Полный integrated suite не завершён.

## Важное ограничение

v131 **не является ещё resumable JVM**. Кадры сейчас являются checkpoint/trace-представлением активных рекурсивных вызовов и сохраняются при завершении. Интерпретатор всё ещё исполняет метод в одном синхронном вызове и не умеет остановить его, вынести PC/locals/operand stack в scheduler, а затем продолжить с того же bytecode PC. Поэтому настоящая независимая многопоточность Java, `sleep/join` с парковкой и полноценный Minecraft game loop пока не реализованы.

Для следующего большого этапа требуется преобразовать цикл интерпретатора в пошаговую машину состояний: `step(frame) -> Yield/Continue/Return/Throw`, а затем хранить отдельные frame stacks для Java-потоков в scheduler.

## Switch status

NRO не собирался; запуск на Nintendo Switch не проверялся. Все перечисленные PASS относятся к host-тестам C++.
