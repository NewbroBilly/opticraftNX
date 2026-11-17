# OptiCraft Switch v151 — загрузка оригинального JAR в libnx NRO

## Что подключено

Добавлена отдельная libnx сборка `OptiCraft-v151-JarLoader.nro`. Она монтирует SD-карту, открывает `sdmc:/switch/opticraft/minecraft.jar`, загружает из ZIP entry `net/minecraft/client/Minecraft.class` и через classfile parser проверяет версию класса и статический метод `main([Ljava/lang/String;)V`. Архив ограничен 128 MiB.

На SD-карте файл нужно разместить по пути `switch/opticraft/minecraft.jar`. Существующий v147 диагностический NRO и его workflow не заменяются; для JAR loader добавлен отдельный workflow.

## Граница этапа

Это реальный libnx-side JAR/classfile load path, но байткод Minecraft всё ещё не исполняется. Он не доказывает запуск игры. Следующая необходимая интеграция — полноценный object interpreter, Java runtime classes, AWT/LWJGL adapters, controller input и покадровое исполнение в Switch event loop.

## Сборка

Локального devkitA64 в этой среде нет, поэтому NRO должен быть собран отдельным GitHub Actions workflow `Build OptiCraft Switch Java archive loader`. Workflow использует devkitPro toolchain и zlib portlib. Физическая проверка NRO и оригинального JAR на Nintendo Switch пока не выполнена.
