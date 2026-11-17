# OptiCraft Switch v138 — VM-wide Class Initialization State

## Что изменено

Добавлен общий для экземпляра `JvmClassRuntime` реестр состояний инициализации классов:

- `Uninitialized`, `Initializing`, `Initialized`, `Erroneous`;
- `begin_class_initialization(name, thread_id, error)` назначает владельца инициализации;
- повторный доступ того же потока к классу в состоянии `Initializing` допускается (рекурсивная инициализация);
- другой поток получает состояние busy, а не начинает второй `<clinit>`;
- `complete_class_initialization` фиксирует успешное завершение только от потока-владельца;
- `fail_class_initialization` помечает класс erroneous и сохраняет причину;
- повторная попытка после erroneous возвращает диагностический `NoClassDefFoundError`.

## Проверки

Отдельно завершились успешно:

1. `run_jvm_class_runtime_test.sh` — включая владение, рекурсивный доступ, конкурирующий поток, успешное завершение и erroneous state.
2. `run_jvm_resumable_object_interpreter_test.sh`.
3. `run_java_thread_execution_runtime_test.sh`.
4. `run_bytecode_verifier_test.sh`.

## Ограничения

- Реестр хранится в `JvmClassRuntime`, то есть общий между интерпретаторами, использующими один runtime instance.
- Реестр пока не защищён mutex/condition variable и не является безопасным для параллельного доступа из настоящих OS threads. Scheduler сейчас cooperative.
- Новый API пока не заменил локальные `initialized_classes_` / `initializing_classes_` внутри `JvmResumableObjectInterpreter`; подключение требует передавать стабильный Java Thread ID в interpreter и корректно обрабатывать ожидание/повтор.
- `ExceptionInInitializerError` ещё не реализован как полноценный Java throwable с причиной; `NoClassDefFoundError` пока диагностическая ошибка API.
- Это host-side C++ тесты. Не подтверждены полный Minecraft JAR, OpenJDK, LWJGL, NRO или запуск на Switch.
