# OptiCraft Switch v149 — корректная классификация текстурных атласов

## Исправление

`RenderEngine` загружает `/terrain.png`, `/gui/items.png` и `/ctm.png`, а поле с соответствующим texture ID устанавливает только после возврата загрузчика. Предыдущий fast path проверял эти поля внутри upload и получал `-1`, из-за чего прозрачные texels атласов проходили как обычные texture pixels.

Добавлен стек текущих resource paths на время вызова `aaw.b(String)`. При upload fast path распознаёт три атласа по активному пути и применяет исходное поведение Minecraft/OptiFine для RGB прозрачных texels.

Диагностическая трасса подтвердила `/terrain.png` → ID 7, `/gui/items.png` → ID 8, `/ctm.png` → ID 9. Во время upload ID-поля были ещё `-1`; теперь path классифицирует все три как atlas.

## Проверки

- `run_jvm_object_interpreter_test.sh` — PASS.
- `run_software_renderer_texture_test.sh` — PASS.
- `run_gl_texture_api_test.sh` — PASS.
- Оригинальный клиент: 1 152 игровых класса, 16 818 классов runtime библиотек, 30 presents за 30 000 000 инструкций, `ORIGINAL_MINECRAFT_FIRST_FRAME=PASS`.
- Главное меню осталось визуально прежним (2 979 уникальных цветов, 402 811 непустых пикселей): оно рисуется заранее подготовленными panorama-картинками, тогда как патч меняет игровые атласы.

Probe останавливается по лимиту в `adz.a(DDD)V`. В диагностике сохраняются NPE в `hu.updateChunkLoading()` и `LWJGLException: Native cursors not supported`.

## До полного запуска

Эта версия улучшает host runtime оригинального клиента, но не является игровым Switch NRO. Runtime v147 не подключён к Switch entrypoint; интеграция JVM, Java-библиотек, AWT/LWJGL замен и игрового цикла в libnx target ещё требуется. На физической Nintendo Switch запуск не подтверждён.
