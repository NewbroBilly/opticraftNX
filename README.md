# OptiCraft Switch — runtime progress v157

Цель проекта — запустить оригинальный OptiCraft JAVA LITE 1.2.5 + OptiFine HD U C7 на Nintendo Switch.

## Что сейчас запускается

GitHub Actions собирает v147 libnx diagnostic NRO: выводит framebuffer, опрашивает кнопки/стики/тачскрин и выходит по Plus. Этот NRO не содержит JVM и не запускает Minecraft.

## Java runtime foundation

Ветка `integration/java-runtime-v151` расширена v152 host-компонентами:
- bounds-checked classfile parser и загрузчик классов из одного JAR;
- ограниченный i32 bytecode interpreter;
- `invokestatic` для `int`-аргументов и `int`/`void`-возврата, включая вызов соседнего класса из того же JAR;
- общий instruction budget, лимит глубины вызовов 64 кадра и явный отказ на неподдерживаемые операции.

Запуск host smoke-проверок JVM:

```sh
sh runtime/jvm/tests/run_classfile_parser_smoke.sh
sh runtime/jvm/tests/run_jar_classloader_smoke.sh
sh runtime/jvm/tests/run_native_registry_smoke.sh
sh runtime/jvm/tests/run_object_runtime_smoke.sh
sh runtime/jvm/tests/run_bytecode_smoke.sh
```

Подробный статус: [`runtime/jvm/LAUNCH_STATUS_RU.md`](runtime/jvm/LAUNCH_STATUS_RU.md).

## Проверка оригинального клиента на host

На локальном оригинальном пакете `OptiCraft JAVA LITE 1.2.5` исправленный v147 host probe загрузил Minecraft, создал окно `854×480` и отрисовал настоящий первый кадр главного меню. За 30 млн инструкций он выдал 30 кадров; probe затем остановился по лимиту инструкций уже в render loop. Графика меню ещё содержит артефакты, а native cursor не поддержан.

Эта проверка выполнена кодом из `OptiCraft_Switch_v147_Original_Main_Menu.zip`; она подтверждает host-side путь v147, но не подключает минимальный v152 runtime к Minecraft или Switch NRO. Текущий v152 `opticraft_launch_preflight` отдельно проверен на оригинальном JAR и подтвердил classfile Java 5, CRC и `main(String[])V`.

Новый v152 C++ classloader тоже проверен на этом JAR: он загрузил `Minecraft.class` и разрешил sibling-класс `aaf` после закрытия исходного archive handle.

## Что ещё нужно для полного запуска

v152 — фундамент исполнения, а не полноценная JVM. Не реализованы многослотовые/объектные значения в interpreter frames, массивы и поля JVM, виртуальные вызовы, исключения, class initialization, classpath нескольких JAR, стандартная Java library и полный JNI/AWT/LWJGL путь. Runtime пока не подключён к Switch NRO; на Switch подтверждён только диагностический libnx слой.

Оригинальный `minecraft.jar` в репозитории не хранится. Для Switch NRO пока требуется связать полноценный interpreter, classpath и AWT/LWJGL backend; локальная проверка v147 остаётся host-side.

## Renderer checkpoint v148 — 2026-11-16

Продолжен host probe оригинального клиента с поддержкой `glColorMask` и исправленным правилом общей границы треугольников. Три целевые графические проверки прошли. Probe снова выдал 30 кадров: 2 979 уникальных цветов, 402 811 непустых пикселей. Диагональные артефакты остались, а метрики меню совпали с предыдущим контрольным прогоном. Выполнение упёрлось в лимит внутри `adz.a(DDD)V` после 30 млн инструкций. NRO с JVM и физический запуск на Switch по-прежнему не подтверждены.

Подробности и снимок: [runtime/jvm/RENDERER_STATUS_V148_RU.md](runtime/jvm/RENDERER_STATUS_V148_RU.md).
Дополнительно: подключён `glPixelStorei`, исправлено соответствие GL-координат texel при линейной фильтрации. Проверки texture API и bridge проходят; два повторных запуска меню оставили те же метрики и артефакты, поэтому диагноз продолжается.
## Renderer atlas correction v149 — 2026-11-17

Host interpreter теперь определяет `/terrain.png`, `/gui/items.png` и `/ctm.png` по активному resource path во время загрузки. Диагностическая трасса подтвердила texture IDs 7, 8 и 9: старые поля RenderEngine были `-1` до завершения upload. Interpreter, texture sampling и GL texture API проверки прошли. Полный host probe снова вывел 30 кадров; меню не изменилось, поскольку показывает предзагруженные panorama-картинки. [Отчёт v149](runtime/jvm/RENDERER_STATUS_V149_RU.md), [архив исходников](OptiCraft_Switch_v149_Atlas_Fix.zip).

Switch status уточнён по локальному и GitHub workflow: текущий NRO — диагностическая libnx оболочка без JVM и Minecraft; пользователь подтвердил на консоли её экран, frame loop и кнопку A. Локальный devkitA64 отсутствует, CI использует контейнер devkitPro. Следующая крупная задача — интеграция JVM/runtime в NRO entrypoint и запуск с JAR/classpath с SD.
## Host startup cleanup v150 — 2026-11-17

Исправлена startup-диагностика курсора: Switch adapter создаёт пустой LWJGL cursor без native desktop handle. Повторный запуск оригинального Minecraft 1.2.5 на host подтвердил 1 152 игровых класса, 16 818 классов библиотек, 30 кадров и `ORIGINAL_MINECRAFT_FIRST_FRAME=PASS`; сообщение о неподдерживаемом native cursor исчезло. Старый NPE `hu.updateChunkLoading()` в этом прогоне не воспроизвёлся.

Визуальные артефакты меню остались. Probe ограничен 30 млн инструкций и остановился в render loop. JVM всё ещё не подключена к libnx NRO; полноценная игра и запуск на физической Switch не подтверждены. Исходники checkpoint: [OptiCraft_Switch_v150_Native_Cursor_Fix.zip](OptiCraft_Switch_v150_Native_Cursor_Fix.zip); [отчёт v150](opticraft_v147_workbench/opticraft_v141_work/REPORT_v150_NATIVE_CURSOR_RU.md).


## Switch JAR loader stage v151 — 2026-11-17

Добавлен отдельный libnx target, который читает оригинальный `minecraft.jar` с SD по пути `switch/opticraft/minecraft.jar`, загружает `net/minecraft/client/Minecraft.class` и проверяет наличие `main(String[])`. Он оставляет v147 hardware diagnostic NRO отдельным и пока не исполняет Java bytecode. Сборка добавлена отдельным GitHub Actions workflow; локального devkitA64 нет, поэтому результат NRO и запуск на консоли требуют CI и аппаратной проверки. [Отчёт v151](opticraft_v147_workbench/opticraft_v141_work/REPORT_v151_SWITCH_JAR_LOADER_RU.md), [исходный архив](OptiCraft_Switch_v151_Switch_Jar_Loader.zip).

## CI assembly repair v158 — 2026-11-17

Исправлен workflow полной игры: он больше не требует отсутствующий архив `OptiCraft_Switch_v152_Original_Game_NRO.zip`, а собирает исходники напрямую из текущего `opticraft_v147_workbench/opticraft_v141_work`. Перед сборкой добавлены проверки ключевых файлов interpreter/runtime. Из `Makefile.switch.game` удалена ссылка на несуществующий `jvm_execution_frames.cpp`; это header-only модель кадров. Теперь workflow может дойти до компиляции и показать реальные ошибки devkitA64/runtime вместо остановки на отсутствующем архиве.

Перед запуском game NRO на SD-карте должен быть подготовлен каталог `switch/opticraft`:

```text
switch/opticraft/minecraft.jar
switch/opticraft/lwjgl.jar
switch/opticraft/lwjgl_util.jar
switch/opticraft/jinput.jar
switch/opticraft/runtime/lib/rt.jar
```

NRO теперь проверяет этот layout и пишет `DATA_READY`, `DATA_MISSING` или `DATA_SIZE_INVALID` в `switch/opticraft/launch.log` до разбора классов. Это позволяет отличить проблему SD-пакета от ошибки JVM.
