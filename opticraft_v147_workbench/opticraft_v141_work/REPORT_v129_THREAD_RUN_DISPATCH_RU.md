# OptiCraft Switch v129 — Java Thread run-dispatch integration

## Что изменено

- `Thread(Runnable)` и `Thread(Runnable, String)` сохраняют ссылку на Runnable в объекте Thread.
- `Thread.start()` теперь проверяет повторный запуск, переводит объект в состояние started/alive и пытается разрешить и выполнить `run()V` через зарегистрированный `JvmClassRuntime`.
- Для Thread-подклассов выбирается виртуальный `run()` самого объекта; для Thread с Runnable вызывается `run()` у сохранённого Runnable.
- `Thread.isAlive()` читает отдельное состояние `thread_alive`; после возврата `run()` оно сбрасывается.
- Повторный `start()` возвращает ошибку `IllegalThreadStateException`.

## Важное ограничение

Это первый вертикальный мост от `Thread.start()` к реальному Java-методу `run()`, но **не полноценная многопоточность Java**. В текущем интерпретаторе вызовы синхронные: `run()` исполняется внутри вызова `start()`. Нет независимого стека/состояния bytecode frame, вытеснения, параллельного выполнения, корректного ожидания `join()` или мониторов `synchronized`. Если `run()` не найден/не поддержан, это не превращает поток в работающий фоновый поток. Планировщик v128 пока остаётся отдельным cooperative native-task прототипом.

## Проверки

Успешно выполнены:
- `tests/run_jvm_java_lang_intrinsics_test.sh`
- `tests/run_jvm_object_interpreter_test.sh`
- `tests/run_java_thread_scheduler_test.sh`

Эти проверки подтверждают отсутствие регрессии в перечисленных тестах, но не доказывают запуск Minecraft или выполнение фоновых Java-потоков на Switch.

## Следующий крупный блок

Нужны resumable Java frames (сохранение PC, locals, operand stack, pending exception и invoke continuation), после чего scheduler сможет приостанавливать и возобновлять именно Java-код. Затем — AWT/LWJGL event loop и передача кадров в Switch framebuffer.
