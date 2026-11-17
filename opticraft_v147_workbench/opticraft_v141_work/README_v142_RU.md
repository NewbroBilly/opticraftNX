# OptiCraft Switch v142

Цель этой контрольной точки — доказать первый framebuffer кадр оригинального Minecraft Java Lite 1.2.5.

## Запуск host-пробника

```bash
tests/run_original_minecraft_main_probe.sh /path/to/minecraft.jar 50000000 /tmp/opticraft-home
```

При успешном получении кадра выводится `ORIGINAL_MINECRAFT_FIRST_FRAME=PASS`, а изображение сохраняется в `tests/build/original_minecraft_frame.png` и `.ppm`.

Это host-проверка. NRO для Switch не входит в подтверждённый результат этой версии.
