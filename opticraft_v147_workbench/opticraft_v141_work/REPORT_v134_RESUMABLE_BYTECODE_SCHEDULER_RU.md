# OptiCraft Switch v134 — Resumable Bytecode + Scheduler

## Итог блока

Добавлен отдельный ограниченный resumable-интерпретатор `ResumableIntegerInterpreter` и подключён к `JavaThreadExecutionRuntime`. В отличие от диагностических snapshots предыдущих версий, этот интерпретатор действительно прекращает исполнение после заданного числа bytecode-инструкций и продолжает с сохранённого `pc`, locals, operand stack и цепочки вызовов.

## Реализовано

- `begin()` создаёт активный Java-кадр статического метода с целочисленными аргументами.
- `step(quantum)` исполняет не более заданного числа инструкций и возвращает управление планировщику со статусом `Yielded`.
- `frames()` показывает текущую цепочку реальных кадров с `class/method/descriptor/pc/locals/operand_stack`.
- `invokestatic` для методов того же класса создаёт дочерний кадр; `ireturn` возвращает значение вызывающему кадру.
- Поддержаны целочисленные константы, локальные переменные, арифметика, условные переходы, `goto`, `tableswitch`, `lookupswitch`, рекурсивные вызовы и integer return.
- Добавлены checkpoint/restore реального состояния интерпретатора с проверкой класса, метода, PC, размера locals и operand stack.
- `JavaThreadExecutionRuntime::create_interpreted_integer()` подключает эти ограниченные bytecode-срезы к cooperative scheduler.
- Несколько потоков чередуются по квантам; тест проверяет парковку одного потока, завершение другого и возобновление первого.
- Runtime checkpoint включает interpreter checkpoint, а не только диагностические кадры.

## Проверки, выполненные в host-среде

- `run_jvm_resumable_interpreter_test.sh` — PASS.
- `run_java_thread_execution_runtime_test.sh` — PASS (включая два bytecode-потока, scheduler interleave и restore).
- `run_jvm_execution_test.sh` — PASS.
- `run_jvm_object_interpreter_test.sh` — PASS.

Это тесты C++ на хосте. NRO на Nintendo Switch не собирался и запуск Minecraft на Switch не подтверждён.

## Существенные ограничения

Это не полная JVM и пока не интерпретатор Minecraft. Resumable-движок поддерживает только подмножество bytecode для `static` методов с параметрами/результатом `int`, а `invokestatic` ограничен методами того же classfile. Не реализованы объекты/ссылки в operand stack этого интерпретатора, instance-вызовы, поля, массивы, float/double/long, monitors, полноценная обработка exception table и JNI. Существующий `ObjectBytecodeInterpreter` Minecraft-пробника остаётся отдельным, синхронным/рекурсивным путём; он ещё не переведён на этот resumable dispatcher.

`ParsedClassCode`, на который ссылаются потоки, должен оставаться живым до завершения этих потоков.

## Следующий крупный этап

Перенести тот же `begin/step/resume` контракт в объектный interpreter: сохранить `ObjectRef`, JVM-значения всех типов, receiver, exception state и continuation вызовов; затем подключить class resolver и heap. После этого — перейти к `Thread`/`Runnable` и AWT/LWJGL bootstrap. Это необходимый шаг до реального запуска оригинального Minecraft 1.2.5.
