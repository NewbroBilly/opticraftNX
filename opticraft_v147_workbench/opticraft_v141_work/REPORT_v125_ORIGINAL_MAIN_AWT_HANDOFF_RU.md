# OptiCraft Switch v125 — выполнение оригинального Minecraft.main и AWT host handoff

## Проверенный результат на ПК

Проверка выполнена на оригинальном пользовательском `minecraft.jar` версии OptiCraft Java Lite 1.2.5. Сам JAR в этот исходный архив не включён.

```text
JAR_CLASSES_REGISTERED=1152
JAR_RESOURCES_REGISTERED=414
MAIN_EXECUTION=RETURNED
INSTRUCTIONS_EXECUTED=891573
MAIN_RETURNED
AWT_WINDOW_CREATES=1
AWT_WINDOW_TITLE=Minecraft
AWT_WINDOW_SIZE=854x480
AWT_WINDOW_VISIBLE=true
ORIGINAL_MAIN_AWT_HOST_HANDOFF=PASS
```

## Что изменено относительно v124

- Исправлено разрешение наследуемых instance fields: поле ищется по цепочке symbolic owner → superclass, без опасного поиска по одному имени поля.
- Исправлено разрешение унаследованных static fields и инициализация declaring class.
- Добавлены типизированные primitive arrays для boolean/byte/char/short/int/long/float/double и соответствующие load/store операции.
- Добавлены базовые runtime intrinsics для HashSet/HashMap/TreeMap, Properties, boxed Integer/Character, Enum, Locale/NumberFormat/DecimalFormat, String, StringBuilder, PrintStream, Thread, InputStream/Reader и коллекционных итераторов.
- Classfile parser сохраняет интерфейсы; assignability теперь учитывает интерфейсы и суперклассы.
- JAR probe загружает не только 1 152 класса, но и 414 ресурсов, поддерживает Class.getResourceAsStream и чтение байтовых потоков.
- Оригинальный probe запускает настоящий `Minecraft.main(String[])` и передаёт AWT операции host callbacks. На host mock получено одно окно `Minecraft` размером 854×480 с visible=true.

## Как повторить

1. Распаковать пользовательский `minecraft.jar` из своего архива.
2. Запустить:

```sh
bash tests/run_original_minecraft_main_probe.sh /path/to/minecraft.jar
```

3. Отдельно проверить AWT callback contract:

```sh
bash tests/run_awt_compatibility_test.sh
```

## Важные ограничения

Это **host-side JVM probe**, а не готовый Switch NRO и не доказательство запуска на консоли. AWT callbacks проверены на host mock; реальное окно Horizon/libnx ещё не подключено к этому interpreter build. Thread.start пока только фиксирует состояние потока, а не запускает независимый поток. OpenGL/LWJGL вызовы, игровая отрисовка, игровой цикл, звук и физическое устройство Switch остаются следующими крупными блоками. `MAIN_RETURNED` означает, что текущая реализация дошла до возврата из Java main, но не означает, что мир Minecraft уже отрисовывается.
