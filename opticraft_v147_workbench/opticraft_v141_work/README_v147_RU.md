# OptiCraft Switch v147

Продолжение порта оригинального OptiCraft JAVA LITE 1.2.5 + OptiFine HD U C7.

**Новый milestone:** оригинальный Minecraft `minecraft.jar` исполняется до настоящего
главного меню на host-side JVM interpreter. Снимок экрана находится в
`SCREENSHOT_MAIN_MENU_HOST_v147.png`.

## Проверки

```bash
bash tests/run_jvm_object_interpreter_test.sh
bash tests/run_gl_compat_core_test.sh
```

Проверка оригинального клиента (после распаковки оригинального пакета):

```bash
bash tests/run_original_minecraft_main_probe.sh \
  "/path/to/mcdata/bin/minecraft.jar" 30000000 "/path/to/mcdata/.minecraft"
```

## Текущие ограничения

Главное меню достигнуто, но в host кадре ещё заметны артефакты текстур/шрифтов.
Кэширование строковых констант добавлено после снимка меню; полный повторный probe
после этого изменения не завершён. NRO не собран, Nintendo Switch hardware не тестировалась.
