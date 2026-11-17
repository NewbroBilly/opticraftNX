# OptiCraft Switch v130 — Thread interruption context

## Изменения
- `Thread.currentThread()` теперь использует контекст текущего интерпретатора, а не process-wide `static` ссылку.
- При синхронном диспетчерировании `Thread.start()` контекст переключается на запускаемый объект и восстанавливается после возврата из `run()`.
- Добавлены intrinsic-операции `Thread.interrupted()` (чтение и очистка флага текущего потока) и `Thread.isInterrupted()` (чтение флага указанного потока без очистки).
- При ошибке в `run()` флаг `thread_alive` сбрасывается перед возвратом ошибки.

## Проверки
- `run_jvm_java_lang_intrinsics_test.sh` — PASS.
- `run_jvm_object_interpreter_test.sh` — PASS.
- `run_java_thread_scheduler_test.sh` — PASS (запуск отдельно).
- `run_jvm_integrated_invocation_test.sh` — не завершился в доступный лимит времени; процесс остановлен по timeout. Это НЕ PASS.

## Ограничения
Это всё ещё синхронный вызов `run()` внутри `start()`. `sleep()` не реализует реальную задержку/парковку; `join()` пока не ожидает завершения. Планировщик v128 остаётся отдельным cooperative C++ callback scheduler и не исполняет сохранённые Java frames. Для настоящей многопоточности требуется преобразовать рекурсивный interpreter execution в resumable frame stack (PC, locals, operand stack, call chain, exception state), а затем подключить его к scheduler.

NRO и запуск на Nintendo Switch в этой версии не подтверждены. Проверки выполнены на host.
