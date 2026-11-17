# OptiCraft Switch v144 — отчёт: frame hook контроллера и LWJGL bootstrap

## Результат

v143 содержал преобразователь кнопок и `InputImplementation`, но его `pump(...)` требовал внешнего вызова. В v144 добавлен конкретный путь опроса Horizon и bootstrap для оригинального LWJGL.

## Реализованные изменения

1. В `src/switch_diagnostic_main.cpp` добавлен C ABI экспорт `opti_switch_read_input(...)`. Он читает `platform_poll_input()` и передаёт JNI состояние кнопок, аналоговых стиков и touch.
2. В `src/switch_input_bridge_jni.cpp` добавлен JNI метод `pollFromHorizon(II)[I`. На Switch (`__SWITCH__`) он получает libnx snapshot, обновляет `SwitchInputBridge` и возвращает упакованные события. В host-сборке без `__SWITCH__` метод намеренно возвращает нейтральный snapshot.
3. `SwitchInputImplementation.pollKeyboard(...)` при включённом Horizon polling вызывает `pumpFromHorizon(...)`. Таким образом штатный LWJGL `Keyboard.poll()` становится точкой опроса на каждом игровом тике; keyboard/mouse events попадают в оригинальные LWJGL очереди.
4. `SwitchInputBootstrap.install()` устанавливает Switch implementation в оригинальные LWJGL 2 `Keyboard` и `Mouse`. Для этого локально подготавливается их статическое состояние, чтобы не запускать desktop `Sys.initialize()`.
5. `SwitchMinecraftLauncher.main(...)` устанавливает input bootstrap до вызова оригинального `net.minecraft.client.Minecraft.main(String[])`.
6. Скрипт сборки `switchport.jar` обновлён: в JAR включены Bridge, InputImplementation, Bootstrap и Launcher; bytecode ограничен Java 8 (major 52).

## Проверки, выполненные в этой версии

- `tests/run_input_adapter_test.sh` — C++ mapping, edge-triggered key/mouse events.
- `tests/run_input_jni_test.sh` — JNI bridge host test.
- `LWJGL_JAR=<original lwjgl.jar> tests/run_input_implementation_test.sh` — LWJGL 2.9.4 `Keyboard`/`Mouse` bootstrap, event queues, wheel, launcher order. Результаты: PASS.
- `tests/run_switch_awt_host_test.sh` — PASS.
- `tests/run_platform_syntax_test.sh` — mock libnx backend + JNI shared-library link + Java JNI run: PASS.
- `tools/build_switchport_jar.sh build/switchport.jar` — JAR проверен, 4 класса, максимальная версия class-file 52: PASS.

## Проверка оригинальной игры

Повторный запуск `tests/run_original_minecraft_main_probe.sh` с оригинальным `minecraft.jar` дал:

```text
JAR_CLASSES_REGISTERED=1152
JAR_RESOURCES_REGISTERED=414
RUNTIME_LIBRARY_CLASSES_REGISTERED=900
RUNTIME_LIBRARY_RESOURCES_REGISTERED=3
MAIN_EXECUTION=STOPPED
INSTRUCTIONS_EXECUTED=30540485
DISPLAY_PRESENTS=2
FRAME_UNIQUE_PIXELS=454
FRAME_NON_BLACK_PIXELS=409920
ORIGINAL_MINECRAFT_FIRST_FRAME=PASS
FIRST_UNSUPPORTED_BOUNDARY=method resolution failed: java/lang/Class.getDeclaredField(Ljava/lang/String;)Ljava/lang/reflect/Field;
```

Первый кадр Mojang splash по-прежнему формируется. Главное меню не достигнуто: текущий custom JVM interpreter не реализует используемый reflection API. Это не тест управления на Switch.

## Ограничения

- Не выполнена сборка NRO.
- Не запускалось на физической Nintendo Switch; mock libnx проверяет только интерфейс/линковку.
- Bootstrap и launcher проверены на host OpenJDK с оригинальным LWJGL 2.9.4. Их исполнение внутри custom JVM проекта не подтверждено.
- Реальный игровой sprint и отдельное меню крафта пока не реализованы; L3/C являются зарезервированными сигналами.
- Для production NRO платформенный backend нужно отделить от диагностического `main()` и включить в конечный launcher/runtime.

## Следующий технический этап

v145: поддержать reflection API, необходимый bootstrap (`Class.getDeclaredField`, `Field.setAccessible`, `Field.get/set`), затем подключить bootstrap к стартовой последовательности custom JVM. После этого повторить оригинальный probe и пройти дальше splash → меню.
