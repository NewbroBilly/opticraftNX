# OptiCraft Switch v145 — AWT host handoff и продолжение запуска

## Что реально проверено

Использован оригинальный `minecraft.jar` из архива OptiCraft JAVA LITE 1.2.5 и соседние `lwjgl.jar`, `lwjgl_util.jar`, `jinput.jar`.

Команда проверки:

```bash
bash tests/run_original_minecraft_main_probe.sh \
  "/path/to/mcdata/bin/minecraft.jar" 50000000
```

Результат:

```text
JAR_CLASSES_REGISTERED=1152
JAR_RESOURCES_REGISTERED=414
RUNTIME_LIBRARY_CLASSES_REGISTERED=900
RUNTIME_LIBRARY_RESOURCES_REGISTERED=3
MAIN_EXECUTION=RETURNED
INSTRUCTIONS_EXECUTED=30541007
DISPLAY_PRESENTS=2
FRAME_UNIQUE_PIXELS=454
FRAME_NON_BLACK_PIXELS=409920
FRAME_CENTER_RGBA=0xff478df6
ORIGINAL_MINECRAFT_FIRST_FRAME=PASS
MAIN_RETURNED
AWT_WINDOW_CREATES=1
AWT_WINDOW_TITLE=Minecraft
AWT_WINDOW_SIZE=854x480
AWT_WINDOW_VISIBLE=true
ORIGINAL_MAIN_AWT_HOST_HANDOFF=PASS
```

Сохранённый кадр `tests/build/original_minecraft_frame.png` визуально показывает заставку Mojang на белом фоне.

## Изменения v145

- Добавлен ограниченный reflection subset: `Class.getDeclaredField`, `Field.setAccessible`, `Field.get`, `Field.getInt`, `Field.set`, `Field.getName`, `Field.getModifiers`.
- Добавлена обработка `monitorenter` / `monitorexit` для однопоточного host probe с проверкой null. Это не является реализацией конкурентных Java-мониторов.
- Расширен AWT host compatibility для `Frame`, `Container`, `Panel`, `Canvas`-производных, `TextArea`, `Color`, `Font`, `StringWriter`, `PrintWriter`, `SimpleDateFormat` и связанных методов, необходимых для прохождения текущей инициализации.
- Добавлена совместимость для `Integer.toHexString` и родственных преобразований.

## Проверки

Прошли отдельные проверки:

- `run_jvm_class_runtime_test.sh`
- `run_jvm_exception_runtime_test.sh`
- `run_gl_compat_core_test.sh`
- `run_gl_texture_api_test.sh`
- `run_client_arrays_test.sh`
- `run_legacy_gl_context_test.sh`
- `run_software_renderer_texture_test.sh`
- `run_framebuffer_blit_test.sh`
- `run_lwjgl_gl11_bridge_test.sh`
- `run_original_minecraft_main_probe.sh` — оригинальный `Minecraft.main` вернулся, host window handoff PASS.

Общий `run_integrated_runtime_suite.sh` не завершился в лимит времени на `run_jvm_object_interpreter_test.sh`; поэтому весь общий suite не объявляется пройденным.

## Что ещё НЕ достигнуто

- Главное меню с кнопками Singleplayer / Multiplayer ещё не отображено. На кадре остаётся Mojang splash.
- Пробник фиксирует два `Display` presents; после создания AWT-окна основной метод возвращается. Следующий этап — выяснить, почему инициализация игрового потока заканчивается до первого кадра меню, и корректно исполнять game loop / thread scheduling.
- Это host-side выполнение, не NRO.
- NRO не собран, Nintendo Switch и libnx hardware не тестировались.
- Reflection и AWT здесь частичные совместимые слои, не полноценные реализации Java SE / AWT.

## Следующий этап

1. Снять точную причину выхода `Minecraft.run()` из стартовой инициализации (сохранить тип/сообщение перехваченного исключения вместо подавления `printStackTrace`).
2. Завершить корректный scheduler для foreground game thread и очередей событий.
3. Добиться настоящего кадра главного меню, затем проверить ввод контроллера.
4. После этого перейти к NRO packaging и проверке на Switch.
