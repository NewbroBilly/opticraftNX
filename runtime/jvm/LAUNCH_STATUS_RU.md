# Запуск OptiCraft Java 1.2.5 — статус v157

Добавлен host-инструмент `opticraft_launch_preflight`. Он открывает настоящий `minecraft.jar`, находит `net/minecraft/client/Minecraft.class`, извлекает запись ZIP (Store/DEFLATE), проверяет CRC32, разбирает constant pool и таблицу методов, проверяет Java 5 classfile (major 49) и наличие `main([Ljava/lang/String;)V` с атрибутом Code.

Запуск проверки на ПК:

```sh
sh runtime/jvm/tests/run_launch_preflight.sh /path/to/minecraft.jar
```

**Важно:** preflight не исполняет Java bytecode и не запускает игру. Это проверяемый первый этап цепочки загрузки. Следующий блок — связать JAR class loader с JVM interpreter, выполнить статические вызовы и затем реализовать объекты/поля/виртуальные вызовы на общей модели runtime. Для реального Minecraft также нужны AWT/Applet compatibility, LWJGL/OpenGL mapping, Java standard library и Switch VM integration.


## Следующий этап — bytecode execution foundation

Добавлен opticraft_bytecode_execute_i32 — ограниченный исполнитель целочисленного Java bytecode с:
- iconst, bipush, sipush;
- iload / istore, iinc;
- iadd, isub, imul, idiv, irem, ineg;
- pop, dup;
- условными переходами ifeq…ifle, if_icmp*, goto;
- ireturn и return.

Исполнитель ограничивает число инструкций, проверяет границы локальных переменных и стека, проверяет цели переходов и явно сообщает о неподдерживаемых opcode. Smoke-тест покрывает арифметику, ветвление, цикл, деление на ноль и отказ на неподдерживаемом opcode.

**Это ещё не JVM и не запуск Minecraft.** v152 добавляет разрешение `CONSTANT_Methodref` и ограниченный `invokestatic` для методов текущего класса и соседних классов того же JAR. Поддерживаются только аргументы типа `int` и возврат `int` либо `void`. Загруженные классы удерживают общий архив, поэтому исходный JAR handle можно закрыть до исполнения. Вызовы делят лимит инструкций, а глубина ограничена 64 кадрами. Неподдерживаемые descriptors, native/abstract методы и несовместимые возвраты завершаются явной ошибкой. Synthetic-JAR smoke проверяет вызов с двумя `int`-аргументами и межклассовый вызов после закрытия исходного JAR handle.

Ограничения остаются значительными: поиск по нескольким JAR/classpath, вызовы Java standard library и native registry не связаны с этим dispatcher; объектные ссылки в operand stack, `getfield`/`putfield`, массивы JVM, `invokevirtual`/`invokeinterface`, исключения, `long`/`float`/`double` и class initialization ещё не реализованы. Исполнитель не подключён к Switch NRO entrypoint. Следующие этапы — многослотовые кадры и объектные ссылки, затем classpath/native linkage и LWJGL/AWT compatibility.

## Проверка оригинального JAR и первого кадра — v153

Исправлены четыре случайных буквальных `\n` в `opticraft_launch_preflight.cpp`, из-за которых утилита не компилировалась. Host preflight собран с `-Wall -Wextra -Werror` и проверен на локальном `minecraft.jar`:

- найден `net/minecraft/client/Minecraft.class`, 32 085 байт, CRC проверен;
- classfile Java 5 (major 49), `main([Ljava/lang/String;)V`, Code — 58 байт;
- результат: `PREFLIGHT_OK`.

Новый v152 C++ JAR classloader также загрузил этот класс напрямую из оригинального архива (`major=49`, `main_code=58`, `max_stack=5`, `max_locals=3`). После закрытия исходного `OptiJar` загрузчик успешно открыл sibling-класс `aaf` из того же удерживаемого JAR.

Отдельно повторён host probe из архива `OptiCraft_Switch_v147_Original_Main_Menu.zip` с оригинальными `minecraft.jar`, `lwjgl.jar`, `lwjgl_util.jar`, `jinput.jar` и bundled `rt.jar`. Он зарегистрировал 1 152 игровых класса и 16 818 классов runtime-библиотек, создал окно `854×480`, выдал 30 presents и подтвердил `ORIGINAL_MINECRAFT_FIRST_FRAME=PASS` при лимите 30 000 000 инструкций. Снимок содержит настоящее главное меню Minecraft 1.2.5; текстуры и шрифт заметно артефактят.

Host probe завершился по instruction budget внутри игрового цикла (`adz.a(DDD)V`). В диагностике также есть `LWJGLException: Native cursors not supported`. Это подтверждает запуск оригинального клиента до первого render loop на host через v147 interpreter; этот код не является минимальным v152 executor и пока не подключён к Switch NRO. Физический запуск на Nintendo Switch не проверялся.

## Renderer checkpoint v148 — 2026-11-16

Повторён host probe оригинального Minecraft 1.2.5 после добавления GL color mask и правила общей границы треугольников. Три целевые проверки графики прошли. Оригинальный клиент выдал 30 кадров при лимите 30 млн инструкций; первый кадр подтверждён, 2 979 уникальных цветов и 402 811 непустых пикселей. Видимые диагональные артефакты остались; метрики кадра не изменились относительно предыдущего контрольного запуска. Probe остановился внутри `adz.a(DDD)V` по instruction budget. Диагностика также содержит неподдерживаемый native cursor и NPE в `hu.updateChunkLoading()`.

Это не полный игровой цикл, не игровой NRO и не проверка на Nintendo Switch. Подробный отчёт и снимок добавлены в `OptiCraft_Switch_v148_Renderer_Fixes.zip`.
## Renderer atlas correction v149 — 2026-11-17

Устранена ошибка fast upload path: RenderEngine присваивает texture ID после вызова загрузки, поэтому чтение полей `terrainTextureId`, `guiItemsTextureId` и `ctmTextureId` во время upload возвращало `-1`. Interpreter теперь передаёт активный resource path через вложенный вызов `aaw.b(String)` и определяет атлас непосредственно по `/terrain.png`, `/gui/items.png` и `/ctm.png`. Трасса подтвердила эти ресурсы для ID 7, 8, 9; все три получили atlas treatment. Interpreter, renderer и GL texture API checks прошли.

Полный host probe достиг 30 presents и первого меню; метрики остались 2 979 уникальных цветов и 402 811 непустых пикселей, поскольку panorama использует отдельные ресурсы. Probe остановился по instruction budget внутри `adz.a(DDD)V`, с NPE `hu.updateChunkLoading` в диагностике. Это не полный игровой цикл.

Проверен GitHub workflow: CI собирает только `OptiCraft-v147-NRO-diagnostic`; NRO не содержит JVM/Minecraft. Ранее пользователь подтвердил на Switch запуск этой диагностической оболочки (экран, frame loop, A button). Локально devkitA64 не найден. Цель остаётся полной: подключить runtime/JVM, оригинальные JAR и игровые input/display к NRO, затем проверить оригинальную игру на консоли. Подробности исправления — `RENDERER_STATUS_V149_RU.md` и `OptiCraft_Switch_v149_Atlas_Fix.zip`.
## Host startup cleanup v150 — 2026-11-17

В v147 host interpreter добавлена обработка создания desktop cursor: OptiFine теперь получает пустой LWJGL cursor object без native handle, а установка native cursor остаётся no-op. Повторный запуск оригинального Minecraft 1.2.5 дал 1 152 игровых класса, 16 818 классов библиотек, 30 presents и `ORIGINAL_MINECRAFT_FIRST_FRAME=PASS`; `LWJGLException: Native cursors not supported` исчезло из диагностики.

Старый NPE `hu.updateChunkLoading()` в повторном запуске не появился; пока он считается невоспроизведённым, отдельный патч не добавлялся. Метрики кадра прежние (2 979 уникальных цветов и 402 811 непустых пикселей), диагональные артефакты не устранены. Host probe остановился по лимиту инструкций в `adz.a(DDD)V`. Это не полноценный NRO и не запуск на Switch. См. [отчёт v150](../../opticraft_v147_workbench/opticraft_v141_work/REPORT_v150_NATIVE_CURSOR_RU.md) и [архив v150](../../OptiCraft_Switch_v150_Native_Cursor_Fix.zip).


## Switch-side JAR loading v151 — 2026-11-17

Добавлен `Makefile.switch.jar-loader` и отдельный workflow. NRO монтирует SD и использует текущие `opticraft_classfile` / `opticraft_jar_classloader`, чтобы загрузить оригинальный Minecraft classfile и проверить статический `main(String[])`. Ожидаемый путь к файлу: `sdmc:/switch/opticraft/minecraft.jar`. v147 diagnostic NRO сохранён отдельным build target.

Этот NRO ещё не исполняет Java. Полноценный object interpreter с host probe пока не интегрирован в libnx loop; текущий minimal v152 executor также не может исполнять Minecraft. NRO не собран локально: devkitA64 отсутствует. GitHub Actions workflow подготовлен, но не запускался из этой рабочей копии; физическая Switch проверка не сделана. См. [отчёт v151](../../opticraft_v147_workbench/opticraft_v141_work/REPORT_v151_SWITCH_JAR_LOADER_RU.md), [архив исходников](../../OptiCraft_Switch_v151_Switch_Jar_Loader.zip).
