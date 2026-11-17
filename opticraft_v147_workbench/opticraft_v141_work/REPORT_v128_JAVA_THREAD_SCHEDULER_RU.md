# OptiCraft Switch v128 — Java thread scheduler foundation

## Большой блок
- Добавлен `JavaThreadScheduler`: ограниченный пул потоков, состояния New/Runnable/Running/Terminated/Failed, однократный `start`, round-robin cooperative dispatch, лимит числа потоков и снимок состояния.
- Каждый шаг выполняется через bounded callback и обязан вернуть управление; это подходит как основа для игрового scheduler на Switch, где нельзя блокировать главный цикл Horizon.
- Добавлен тест жизненного цикла, round-robin продвижения, повторного `start`, завершения, исключения callback и лимита потоков.
- Новый тест включён в интеграционный host suite.

## Проверки
- `tests/run_java_thread_scheduler_test.sh` — PASS.
- Полный интеграционный suite — см. `tests/VALIDATION_v128.txt`.

## Важное ограничение
Это scheduler foundation, а не законченная реализация `java.lang.Thread`: interpreter пока не передаёт сюда Java call frames и `Thread.start()` в `jvm_object_interpreter.cpp` всё ещё не исполняет `run()` в отдельном Java-контексте. Для настоящего Minecraft требуется следующий блок: resumable interpreter frames + Runnable/Thread target resolution + `start/join/sleep/isAlive` integration. Этот checkpoint не является NRO и не подтверждает запуск Minecraft на Switch.
