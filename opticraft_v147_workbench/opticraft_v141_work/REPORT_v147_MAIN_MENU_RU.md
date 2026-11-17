# OptiCraft Switch v147 — достигнуто оригинальное главное меню

## Результат

Оригинальный пакет `OptiCraft JAVA LITE 1.2.5.zip` найден в Library, извлечён и
использован для host-side запуска `mcdata/bin/minecraft.jar` вместе с оригинальными
`lwjgl.jar`, `lwjgl_util.jar`, `jinput.jar` и bundled `rt.jar`.

**Подтверждено:** получен настоящий кадр главного меню оригинального Minecraft Java
1.2.5. На снимке присутствуют оригинальные элементы `Singleplayer`, `Multiplayer`,
`Texture Packs`, `Options`, `Quit Game`, логотип и подпись версии. Экран не нарисован
вручную и не заменяет игровые классы.

## Данные запуска, в котором получен кадр меню

- `JAR_CLASSES_REGISTERED=1152`
- `JAR_RESOURCES_REGISTERED=414`
- `RUNTIME_LIBRARY_CLASSES_REGISTERED=16818`
- `RUNTIME_LIBRARY_RESOURCES_REGISTERED=3`
- `INSTRUCTIONS_EXECUTED=24243030`
- `DISPLAY_PRESENTS=18`
- Host window: `854x480`, title `Minecraft`, visible.
- `FRAME_UNIQUE_PIXELS=3043`
- `FRAME_NON_BLACK_PIXELS=402795`
- Снимок: `SCREENSHOT_MAIN_MENU_HOST_v147.png`

После отображения меню probe завершился с `string constant allocation failed`.
Это ограничение host VM heap при повторном создании одинаковых строковых констант,
а не доказательство полного прохождения игрового цикла.

## Изменения v147

1. Быстрая инициализация таблицы синусов `gk` оригинального клиента.
2. C++ fast path загрузки обычных текстур для профиля без mipmap/anisotropic/AA.
3. Java 6 Locale default (`en_US`) и совместимость `Calendar.setWeekCountData`.
4. Поддержка `Class.getFields`, `Field.getType`, `Class.equals` и JVM `ConstantValue`.
5. Поддержка NIO directness, `Buffer.remaining`, FloatBuffer put/get и typed views.
6. Host LWJGL `DisplayImplementation`, `DisplayMode` и обход desktop Linux backend.
7. Поддержка GL `glLoadMatrix` / `glMultMatrix` через FloatBuffer и матричный backend.
8. Поддержка распространённых Charset (`ISO-8859-1`, UTF-8, US-ASCII, UTF-16).
9. Добавлено кэширование строковых и Class-констант `ldc`, чтобы не выделять новые
   объекты при каждом повторном выполнении одной и той же константы.

## Проверки

- `tests/run_jvm_object_interpreter_test.sh` — PASS после добавления кэша констант.
- `tests/run_gl_compat_core_test.sh` — PASS после добавления загрузки/умножения матриц.
- Оригинальный probe до добавления последнего кэша — достиг настоящего главного меню,
  затем остановился на повторном выделении строковой константы.
- Повторный полный probe после последнего изменения кэша не завершён в этом блоке;
  не объявляется пройденным.

## Ограничения

- На снимке меню видны графические артефакты: текст/текстуры отображаются с шумом и
  неполной корректностью. Следующий этап — исправление растеризации, шрифтов и
  текстурного состояния, затем проверка на нескольких кадрах и интерактивного ввода.
- Это host-side milestone, не собранный игровой NRO.
- Сборка через devkitPro/libnx и проверка на физической Nintendo Switch не выполнены.
