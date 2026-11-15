# Запуск OptiCraft Java 1.2.5 — статус v141

Добавлен host-инструмент `opticraft_launch_preflight`. Он открывает настоящий `minecraft.jar`, находит `net/minecraft/client/Minecraft.class`, извлекает запись ZIP (Store/DEFLATE), проверяет CRC32, разбирает constant pool и таблицу методов, проверяет Java 5 classfile (major 49) и наличие `main([Ljava/lang/String;)V` с атрибутом Code.

Запуск проверки на ПК:

```sh
sh runtime/jvm/tests/run_launch_preflight.sh /path/to/minecraft.jar
```

**Важно:** preflight не исполняет Java bytecode и не запускает игру. Это проверяемый первый этап цепочки загрузки. Следующий блок — связать JAR class loader с JVM interpreter, выполнить статические вызовы и затем реализовать объекты/поля/виртуальные вызовы на общей модели runtime. Для реального Minecraft также нужны AWT/Applet compatibility, LWJGL/OpenGL mapping, Java standard library и Switch VM integration.


## Следующий этап — bytecode execution foundation

Добавлен opticraft_bytecode_execute_i32 — ограниченный исполнитель целочисленного Java bytecode с:
- iconst, bipush, sipush;
- iload / istore, iinc;
- iadd, isub, imul, idiv, irem, ineg;
- pop, dup;
- условными переходами ifeq…ifle, if_icmp*, goto;
- ireturn и return.

Исполнитель ограничивает число инструкций, проверяет границы локальных переменных и стека, проверяет цели переходов и явно сообщает о неподдерживаемых opcode. Smoke-тест покрывает арифметику, ветвление, цикл, деление на ноль и отказ на неподдерживаемом opcode.

**Это ещё не JVM и не запуск Minecraft.** Теперь classfile parser сохраняет Code bytes и max_stack/max_locals, JAR classloader отдаёт байткод загруженного метода, а method executor передаёт его в этот interpreter. Smoke-тест проверяет путь от синтетического JAR до исполнения return. Но это ещё не полноценный JVM dispatcher: не поддерживаются вызовы методов, constant-pool resolution, объекты в operand stack, getfield/putfield, массивы JVM, invokestatic/invokevirtual, исключения, long/float/double, class initialization или Java standard library. Исполнитель пока не подключён к Switch NRO entrypoint. Следующий необходимый этап — полноценный frame/call-stack и constant-pool-aware dispatcher, затем системные классы и LWJGL/AWT compatibility.
