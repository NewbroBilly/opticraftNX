# OptiCraft Switch v136 — Object Runtime Expansion

## Краткий итог

Продолжена ветка v135. Resumable object interpreter получил сериализуемый снимок активной цепочки кадров, восстановление кадров, обработку ограниченного набора массивных и статических инструкций и переход по exception table для явного `athrow`. Checkpoint/restore объектного интерпретатора подключён к `JavaThreadExecutionRuntime`.

Это host-side C++ runtime prototype. Это не полная JVM и не запуск Minecraft на Nintendo Switch.

## Изменения

### 1. Object interpreter checkpoint / restore
- `Snapshot` содержит цепочку кадров с именем класса/метода/дескриптором, PC, receiver, locals и operand stack.
- Сохраняются status, итоговое значение, суммарное число инструкций и текст ошибки.
- `restore()` повторно разрешает методы в текущем `JvmClassRuntime` и проверяет границы PC/locals/stack, глубину вызовов и валидность ссылок.
- Снимок не хранит сырые указатели на `ParsedClassCode` / `MethodCode`.

### 2. Thread runtime integration
- `JavaThreadExecutionRuntime::Checkpoint` теперь содержит object-interpreter snapshot.
- `checkpoint()` и `restore()` умеют сохранять и восстанавливать объектный интерпретатор запланированного потока.
- Обновлён runtime test runner: он теперь линкует object interpreter и class/heap runtime.

### 3. Bytecode coverage
- `getstatic`, `putstatic` с JVM default values для ещё не определённых static fields.
- `newarray` для primitive array types, `anewarray` для reference arrays.
- `iaload`, `iastore`, `aaload`, `aastore`, `arraylength`.
- `checkcast`, `instanceof`.
- `athrow` с поиском подходящего диапазона `start_pc..end_pc` в exception table, проверкой catch type и переходом на handler PC.

## Проверки, выполненные на host

PASS:
- resumable object interpreter: nested frames, checkpoint/restore, arrays, static fields, `athrow` handler и scheduler checkpoint/restore;
- legacy object interpreter;
- JVM execution;
- JVM heap/runtime;
- Java thread scheduler;
- Java thread execution runtime;
- class runtime;
- exception runtime table lookup;
- bytecode verifier;
- class linker;
- AWT compatibility;
- resumable integer interpreter.

Это 12 отдельных host test runners, не полный integrated suite. Полный `run_integrated_runtime_suite.sh` в рамках v136 не запускался.

## Ограничения

- Exception-table dispatch пока применяется к явному `athrow`; ошибки типа division-by-zero, null dereference, bounds и неудачный `checkcast` пока не создают полноценные Java exception objects и не проходят общий exception dispatcher.
- `athrow null` пока диагностируется как NullPointerException failure, а не создаёт/диспетчеризует объект `java/lang/NullPointerException`.
- `aastore` проверяет ссылочный тип значения и границы, но ещё не проверяет компонентную assignability для `ArrayStoreException`.
- `anewarray` поддерживает обычный компонентный класс; многомерные массивы и полный набор primitive array load/store opcode не завершены.
- `getstatic`/`putstatic` работают через `JvmStaticFields`, но class initialization (`<clinit>`) и ConstantValue attributes ещё не реализованы.
- Scheduler checkpoint сохраняет object interpreter и frame view; полное восстановление внутреннего состояния самого scheduler (очередь, cursor, interrupt flags и все thread states) пока не сделано.
- Старый `ObjectBytecodeInterpreter` не заменён автоматически новым resumable runtime.
- Нет полного JNI/LWJGL/OpenGL, полноценной Java 8 JVM, NRO build или подтверждённого запуска Minecraft на Switch.

## Следующий этап v137

- Общий Java exception object factory и диспетчеризация runtime exceptions через exception tables.
- Проверка component type в `aastore` и поддержка `anewarray` с assignability.
- `get/putstatic` с class initialization и ConstantValue.
- Расширение opcode coverage под реальные классы Minecraft 1.2.5.
- Начать интеграцию resumable runtime с оригинальным JAR probe вместо отдельного fixture-only пути.
