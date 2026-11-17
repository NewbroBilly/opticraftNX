# OptiCraft Switch v146 — исходный проект

Продолжение оригинального OptiCraft Java Lite 1.2.5 + OptiFine HD U C7.

## Проверка JVM и диагностики

```bash
bash tests/run_jvm_automatic_exception_test.sh
bash tests/run_jvm_object_interpreter_test.sh
bash tests/run_jvm_integrated_invocation_test.sh
```

## Проверка оригинального Minecraft

Распакуйте оригинальный пакет OptiCraft так, чтобы были доступны `mcdata/bin/minecraft.jar`, `lwjgl.jar`, `lwjgl_util.jar`, `jinput.jar` и `mcdata/runtime/lib/rt.jar`.

```bash
bash tests/run_original_minecraft_main_probe.sh \
  "/path/to/mcdata/bin/minecraft.jar" 50000000 "/path/to/user-home"
```

Пробник выводит `JAVA_DIAGNOSTICS_BEGIN/END`, а также последние Java call frames. Эти данные нужны, чтобы найти фактическую причину завершения стартового потока.

Для тестов, извлекающих файлы из внешнего архива, задайте:

```bash
export OPTICRAFT_ZIP="/path/to/OptiCraft JAVA LITE 1.2.5.zip"
```

## Честный статус

v146 улучшает диагностику JVM на host. Это не готовый NRO и не доказательство достижения главного меню. Оригинальная игра пока подтверждена до заставки Mojang и AWT host handoff (v145).
