# OptiCraft Switch v142 — первый кадр оригинального Minecraft

## Результат

Оригинальный `minecraft.jar` из OptiCraft Java Lite 1.2.5 исполняется в host JVM-пробнике до вызова LWJGL `Display.swapBuffers()`. Callback `LwjglGL11Bridge` получает framebuffer 854x480 и сохраняет его в `tests/build/original_minecraft_frame.png`.

Захваченный кадр визуально содержит оригинальный splash-экран Mojang (логотип на белом фоне). Это первый подтверждённый кадр оригинального клиента, а не вручную нарисованная заставка.

## Исправления v142

- Добавлена обработка LWJGL `GL11.glColorPointer(int, boolean, int, ByteBuffer)` и однотипных `glVertexPointer` / `glTexCoordPointer` перегрузок.
- Данные NIO ByteBuffer копируются с учётом position/limit и передаются в host GL client-array storage.
- Добавлены `Math.sin`, `Math.cos`, `Math.tan`, `Math.toRadians`, `Math.toDegrees` в JVM intrinsic handling.
- `Display.swapBuffers()` подключён к тому же present callback, что и `Display.update()`.
- Probe сохраняет PNG/PPM кадр, считает уникальные/нечёрные пиксели и возвращает PASS только при наличии непустого framebuffer.

## Проверка

Команда:

```bash
tests/run_original_minecraft_main_probe.sh /path/to/minecraft.jar 50000000 /tmp/opticraft-home
```

Результат последнего запуска:

- JAR classes registered: 1152
- JAR resources registered: 414
- Instructions executed: 30,540,464
- Display presents: 2
- Frame size: 854x480
- Unique pixel values: 454
- Non-black pixels: 409,920
- `ORIGINAL_MINECRAFT_FIRST_FRAME=PASS`
- Следующая неподдержанная точка после захвата кадра: `org/lwjgl/input/Mouse.create()V`

LWJGL graphics tests (7 отдельных скриптов): PASS:

1. GL compatibility core
2. GL texture API
3. client arrays
4. legacy GL context
5. software renderer texture sampling
6. framebuffer blit
7. LWJGL GL11 bridge

Полный общий runner в одной сессии был остановлен по тайм-ауту на software renderer test; этот тест и оставшиеся тесты затем были запущены отдельно и прошли.

## Ограничения

- Это host software-renderer framebuffer, не Switch GPU output.
- Кадр — Mojang splash, а не главное меню.
- `Minecraft.main()` пока не завершает старт: после двух present calls останавливается на `Mouse.create()`.
- Не реализованы полноценные LWJGL input backends, все JNI entrypoints и аппаратный Switch graphics backend.
- NRO не собран и на Nintendo Switch не тестировался.
