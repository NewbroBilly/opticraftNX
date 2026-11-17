# OptiCraft Switch v144 — подключение контроллера к LWJGL

В v144 добавлен путь опроса контроллера от libnx до оригинальных LWJGL `Keyboard` и `Mouse`.

## Что добавлено

- `opti_switch_read_input(...)` — экспорт текущего состояния Npad, стиков и touch из Horizon backend.
- JNI `SwitchInputBridge.pollFromHorizon(width, height)` — забирает snapshot libnx и преобразует его в события LWJGL.
- `SwitchInputImplementation.pollKeyboard(...)` вызывает Horizon polling; штатный `Keyboard.poll()` становится точкой опроса на каждом игровом тике.
- `SwitchInputBootstrap.install()` устанавливает один Switch `InputImplementation` в оригинальные LWJGL `Keyboard` и `Mouse`, без вызова desktop `Sys.initialize()`.
- `SwitchMinecraftLauncher` устанавливает адаптер до вызова `net.minecraft.client.Minecraft.main(String[])`.
- `tools/build_switchport_jar.sh` собирает `build/switchport.jar` под Java 8 (class-file major 52).

## Проверки

```bash
tests/run_input_adapter_test.sh
tests/run_input_jni_test.sh
LWJGL_JAR="/path/to/original/lwjgl.jar" tests/run_input_implementation_test.sh
tests/run_switch_awt_host_test.sh
tests/run_platform_syntax_test.sh
LWJGL_JAR="/path/to/original/lwjgl.jar" tools/build_switchport_jar.sh build/switchport.jar
```

`run_platform_syntax_test.sh` компилирует JNI вместе с libnx backend на mock `switch.h`, линкует их в shared library и запускает JNI-тест через неё. Это проверяет символы и путь вызова, но не заменяет сборку devkitPro/NRO.

## Ограничения

- В v144 проверены host OpenJDK и mock libnx. Настоящая Nintendo Switch и Joy-Con/Pro Controller не проверялись.
- NRO не собирался. Реальная сборка должна включить `switch_diagnostic_main.cpp` (как платформенный backend без диагностического `main` в финальном приложении), `switch_input_bridge.cpp`, `switch_input_bridge_jni.cpp`, `input_adapter.cpp` и JNI runtime.
- В проектном custom-JVM probe оригинальный Minecraft всё ещё не доходит до главного меню. После регистрации 1152 классов игры и 900 классов библиотек он останавливается на `java/lang/Class.getDeclaredField(Ljava/lang/String;)Ljava/lang/reflect/Field;`. Это следующий JVM blocker; кадр Mojang splash продолжает проходить.
- Установка LWJGL bootstrap прошла на host OpenJDK с оригинальным LWJGL 2.9.4. Совместимость bootstrap с текущим custom JVM interpreter ещё не подтверждена.
- L3 пока выдаёт Left Control как сигнал под будущий sprint; vanilla 1.2.5 сам по себе не имеет sprint-механики. Y выдаёт C как зарезервированную команду меню крафта; отдельное меню требует игровой логики.

## Статус

| Компонент | Состояние |
|---|---|
| Карта кнопок и стиков | Реализована, host-тесты PASS |
| JNI event bridge | PASS |
| Установка LWJGL Keyboard/Mouse | PASS на host OpenJDK + LWJGL 2.9.4 |
| Автоматический опрос через `Keyboard.poll()` | PASS на host JNI/mock |
| JNI → libnx backend linkage | PASS на mock libnx |
| Запуск оригинального Minecraft | Пока только splash frame; main menu не достигнуто |
| NRO / физическая Switch | Не проверено |
