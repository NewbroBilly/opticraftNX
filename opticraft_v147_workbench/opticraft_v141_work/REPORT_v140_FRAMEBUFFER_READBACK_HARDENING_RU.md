# OptiCraft Switch v140 — безопасное чтение framebuffer

## Изменения относительно v139

- Укреплён `LwjglGL11Bridge::read_pixels_rgba8`.
- Проверки координат выполняются через безопасное вычитание, без потенциального переполнения `x + w` / `y + h`.
- Добавлена проверка переполнения при вычислении размера RGBA8-буфера (`width * height * 4`).
- Индексы исходного framebuffer и смещения назначения рассчитываются через `size_t` после проверки границ.
- Добавлены регрессионные проверки для чрезмерно больших размеров, координат за пределами `int`-диапазона framebuffer и недостаточного выходного буфера.

## Проверка

`tests/run_lwjgl_graphics_suite.sh` — PASS, все 7 тестов:
1. GL compatibility core
2. texture API
3. client arrays
4. legacy GL context
5. software renderer texture sampling
6. framebuffer blit
7. LWJGL GL11 bridge (включая новые проверки readback)

`tests/run_integrated_runtime_suite.sh` — частично пройден: JVM runtime, class runtime, exception runtime, execution, resumable interpreter и object interpreter прошли; процесс остановлен по тайм-ауту на `run_jvm_resumable_object_interpreter_test.sh`. Это не засчитывается как полный PASS.

## Границы готовности

Это host-side C++ исправление безопасности framebuffer readback. Оно не подключает само по себе JNI-вызовы `org.lwjgl.opengl.GL11`, не реализует полный `Display` lifecycle и не доказывает первый кадр Minecraft. Switch NRO и запуск на консоли не проверены.

## Следующая задача

Подключить GL11 bridge к реальному JNI dispatch рантайма, затем связать `Display.create/update/swapBuffers/destroy` с libnx framebuffer lifecycle и прогнать оригинальный Minecraft startup probe.
