# OptiCraft Switch v150 — Native cursor handling

## Исправление

OptiFine 1.2.5 создаёт org.lwjgl.input.Cursor при старте AWT mouse helper. LWJGL кидает LWJGLException, если backend возвращает нулевые native cursor capabilities. На Switch системный указатель не используется: ввод должен идти через контроллер.

В object interpreter constructor LWJGL Cursor для указанной сигнатуры теперь создаёт пустой cursor object без desktop native handle. Вызов Mouse.setNativeCursor также остаётся no-op и возвращает null. Это устраняет безвредное исключение старта и не меняет контроллерный ввод.

## Проверка оригинального клиента на host

- Зарегистрировано 1 152 класса Minecraft и 16 818 классов runtime библиотек.
- При лимите 30 000 000 инструкций клиент выдал 30 кадров и ORIGINAL_MINECRAFT_FIRST_FRAME=PASS.
- В JAVA_DIAGNOSTICS больше нет LWJGLException: Native cursors not supported.
- Кадр совпадает по метрикам с v149: 2 979 уникальных цветов, 402 811 непустых пикселей. Видимые артефакты GUI сохраняются.
- Исполнение остановлено лимитом внутри adz.a(DDD)V, как и ожидается для бесконечного игрового цикла в конечном host probe.

Старый NPE hu.updateChunkLoading() не воспроизвёлся в этом прогоне; отдельная правка для него не делалась.

## Ограничения

Это host-only изменение. Полноценный JVM не подключён к libnx NRO, графические артефакты не устранены, полноценный игровой цикл и физическая Switch не подтверждены.
