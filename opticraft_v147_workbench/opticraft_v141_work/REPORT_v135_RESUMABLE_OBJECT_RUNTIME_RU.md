# OptiCraft Switch v135 — Resumable Object Runtime

## Результат

Добавлен отдельный typed/reference-aware resumable interpreter как следующий миграционный слой между integer-пробником v134 и существующим ObjectBytecodeInterpreter. Он работает с теми же `JvmHeap`, `JvmStaticFields` и `JvmClassRuntime`, но пока не заменяет старый interpreter автоматически.

## Реализовано

- `begin_static()` и `begin_instance()` для запуска статического и объектного метода.
- Независимые resumable frames с `pc`, `receiver`, `locals` и `operand_stack` типа `JvmValue`.
- Ограниченное исполнение через `step(quantum)` и `run_to_completion()`.
- `invokestatic`, `invokevirtual`, `invokespecial` через `JvmClassRuntime::resolve_method`, с отдельными кадрами вызовов.
- Подмножество typed bytecode: int/reference locals, null, integer arithmetic, branches, `new`, `dup`, `getfield`, `putfield`, integer/reference/void return.
- Проверки типов ссылок, границ locals/stack, receiver и call-depth.
- Новый путь `JavaThreadExecutionRuntime::create_interpreted_object()` запускает объектный interpreter как cooperative scheduler task и публикует активные кадры в thread context.
- Тестирует static nested call, instance method, heap field read, квантовую остановку и выполнение через scheduler.

## Проверки

См. `VALIDATION_v135.txt`. Все перечисленные PASS — host C++ tests. Они не означают готовность полной JVM.

## Ограничения

- Это пока ограниченное bytecode-подмножество, а не полная JVM: нет category-2 long/double, float arithmetic, массивных opcode, exception-table dispatch, monitors, class initialization, JNI и полного verifier enforcement.
- `new` выделяет instance layout, но полноценная семантика constructor цепочек и platform constructors ещё не завершена.
- Поля instance поддержаны; static field bytecode ещё не включён в resumable путь.
- Scheduler object path не имеет полноценного object-interpreter checkpoint/restore API; frame views пока публикуются как снимки диагностики.
- Старый `ObjectBytecodeInterpreter` Minecraft probe пока остаётся отдельным синхронным исполнителем. Его автоматическая замена этим runtime ещё не выполнена.
- NRO/Horizon OS build и запуск оригинального Minecraft 1.2.5 не проверялись.

## Следующий блок v136

Перенести exception-table dispatch и object checkpoint/restore в resumable object runtime, затем расширять opcode coverage (arrays, static fields, type checks) и начать перевод вызовов старого Minecraft object interpreter на общий resolver/heap path.
