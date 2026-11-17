# OptiCraft Switch v127 — AWT host adapter checkpoint

## Добавлено
- `include/switch_awt_host.hpp` и `src/switch_awt_host.cpp` — адаптер AWT top-level window для Switch.
- Адаптер принимает `create_window` / `set_visible`, сохраняет заголовок и логический размер окна и передаёт RGBA8 кадр в libnx framebuffer только после создания и показа окна.
- Добавлены `tests/switch_awt_host_test.cpp` и `tests/run_switch_awt_host_test.sh`.

## Проверки
- Switch AWT host adapter test — PASS.
- AWT compatibility: Frame / Canvas / BorderLayout / Dimension → host callbacks — PASS.
- Integrated JVM calls: cross-class dispatch, constructors, inherited fields, cross-frame exception handling — PASS.
- Bytecode verifier — PASS.
- Class linker preparation — PASS.
- Classfile inspector: 1,152 классов, 9,112 методов с Code, 633 exception handlers; все 9,112 методов прошли bytecode verification — PASS.
- Original Minecraft 1.2.5 main probe ранее подтверждён: 1,152 класса и 414 ресурсов зарегистрированы; `Minecraft.main` вернул управление после 891,573 инструкций; AWT host mock получил окно `Minecraft` 854×480 и visible=true.

## Ограничения
- Проверки — host/PC, не тест на Nintendo Switch.
- В текущем окружении нет `DEVKITPRO` и `aarch64-none-elf-g++`; NRO здесь не собран.
- `Thread.start()` пока только устанавливает `thread_started`; независимый/кооперативный поток и игровой цикл Minecraft не исполняются.
- Адаптер ещё не включён в загрузочный путь NRO и не получает кадры от игрового рендерера.
- Полная реализация LWJGL/OpenGL и загрузка JAR на Switch не завершены.
- Оригинальный `minecraft.jar` намеренно не включён в архив.

## Следующий блок
Реализовать планировщик Java-потоков и безопасную передачу управления; связать игровой цикл с AWT host и libnx framebuffer; затем добавить SD classpath и собрать NRO в окружении с devkitPro.
