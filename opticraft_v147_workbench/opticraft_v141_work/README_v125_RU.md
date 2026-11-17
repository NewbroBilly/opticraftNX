# OptiCraft Switch v125 — source package

В этом архиве находится исходный пакет JVM/runtime, тесты и инструменты проверки. Оригинальный `minecraft.jar` намеренно не включён: используйте собственную копию из пользовательского архива.

Сначала прочитайте `REPORT_v125_ORIGINAL_MAIN_AWT_HANDOFF_RU.md`.

Проверка запуска на ПК:

`bash tests/run_original_minecraft_main_probe.sh /path/to/minecraft.jar`

Проверка AWT host callback:

`bash tests/run_awt_compatibility_test.sh`

Внимание: это ещё не Switch NRO с полноценной графикой. Физический запуск на консоли не проверен.

## Актуальная инженерная версия: v137

Последний host-side runtime блок описан в `REPORT_v137_RUNTIME_EXCEPTIONS_CLASS_INIT_RU.md`.
Он добавляет exception unwinding между вложенными resumable Java-кадрами, runtime exception objects,
проверку component type для `aastore`, разбор `ConstantValue` и resumable запуск `<clinit>`.
Проверки перечислены в `VALIDATION_v137.txt`. Это всё ещё прототип JVM на host: NRO и запуск
оригинального Minecraft на Nintendo Switch не подтверждены.
