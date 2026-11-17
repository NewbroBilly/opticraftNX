# OptiCraft Switch v146 — диагностика запуска оригинального Minecraft

## Цель блока

В v145 оригинальный `Minecraft.main()` возвращался после двух framebuffer presents и передачи окна `Minecraft` размером 854x480. На экране оставалась заставка Mojang; главное меню не было достигнуто. Причина завершения игрового потока не была видна, поскольку `Throwable.printStackTrace()` подавлялся.

## Изменения v146

1. Добавлен ограниченный диагностический буфер JVM (до 64 KiB).
2. Исключения, созданные VM, сохраняют тип, сообщение, класс/метод и bytecode PC.
3. Для текущего host probe добавлена совместимость `Throwable(String)`, `Throwable(Throwable)`, `getMessage()`, `getLocalizedMessage()`, `toString()` и `printStackTrace()`.
4. `printStackTrace()` больше не исчезает бесследно: запись попадает в диагностический вывод, а при `PrintWriter(StringWriter)` также в строковый буфер Java.
5. Пробник печатает `JAVA_DIAGNOSTICS_BEGIN/END` и последние 24 завершённых Java call frame.
6. Исправлены test runner link flags для объектного JVM-интерпретатора (GL bridge, zlib, libpng).
7. Тесты, которым нужен внешний оригинальный пакет OptiCraft, теперь явно пишут `SKIP`, если `OPTICRAFT_ZIP` не задан, вместо аварийного падения на отсутствующем локальном файле.

## Проверки, выполненные в этой среде

PASS:
- `run_jvm_runtime_test.sh`
- `run_jvm_class_runtime_test.sh`
- `run_jvm_exception_runtime_test.sh`
- `run_jvm_automatic_exception_test.sh` — дополнен проверкой сохранения сообщения и call frame при `printStackTrace()`
- `run_jvm_execution_test.sh`
- `run_jvm_resumable_interpreter_test.sh`
- `run_jvm_resumable_object_interpreter_test.sh`
- `run_jvm_java_lang_intrinsics_test.sh`
- `run_jvm_execution_frames_test.sh`
- `run_jvm_integrated_invocation_test.sh`
- `run_jvm_object_interpreter_test.sh`
- `run_bytecode_verifier_test.sh`
- `run_class_linker_test.sh`
- `run_awt_compatibility_test.sh`
- `run_java_thread_scheduler_test.sh`
- `run_java_thread_execution_runtime_test.sh`
- `run_switch_awt_host_test.sh`

SKIP:
- `run_classfile_inspector_test.sh`
- `run_class_loading_graph_test.sh`
- `run_classpath_resolver_test.sh`
- `run_runtime_classpath_test.sh`

Причина SKIP: в текущей рабочей среде отсутствует исходный пользовательский архив `OptiCraft JAVA LITE 1.2.5.zip`. Для этих проверок нужно задать `OPTICRAFT_ZIP=/path/to/OptiCraft\ JAVA\ LITE\ 1.2.5.zip`.

`run_integrated_runtime_suite.sh` не завершился в лимит 300 секунд на `run_jvm_object_interpreter_test.sh`; этот тест отдельно прошёл при запуске самостоятельно. Полный общий suite поэтому не объявляется пройденным.

## Состояние оригинальной игры

Последняя проверка из v145 остаётся подтверждённой только для host probe:
- 1152 класса оригинального JAR зарегистрированы;
- `Minecraft.main()` вернулся;
- создано и показано AWT host window `Minecraft`, 854x480;
- framebuffer содержит кадр заставки Mojang;
- главное меню Singleplayer / Multiplayer не достигнуто;
- NRO не собран, физическая Nintendo Switch не тестировалась.

В v146 сам оригинальный JAR повторно не запускался: оригинальный пакет и соседние LWJGL/JInput/runtime JAR отсутствуют в текущей рабочей среде. Новая диагностика готова для следующего запуска с пользовательским архивом.

## Следующий блок

Запустить `run_original_minecraft_main_probe.sh` с оригинальным `minecraft.jar`, получить `JAVA_DIAGNOSTICS` и последние call frames, затем устранить конкретную причину выхода `Minecraft.run()` и продолжить до первого настоящего кадра главного меню. Не рисовать поддельное меню и не заменять оригинальные классы.
