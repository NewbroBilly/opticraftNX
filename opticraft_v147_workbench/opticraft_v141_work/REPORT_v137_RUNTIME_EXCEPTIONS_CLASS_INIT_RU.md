# OptiCraft Switch v137 — Runtime Exceptions & Class Initialization

## Итог

Продолжена исходная инженерная ветка v136. В resumable object interpreter добавлены Java runtime exception objects, поиск обработчиков через exception tables с раскруткой вложенных Java-кадров, проверка типов для `aastore`, разбор `ConstantValue` и запуск `<clinit>` перед первым активным доступом к static field.

Это host-side C++ runtime. Это **не** полноценная Java SE JVM, не готовый порт Minecraft и не подтверждение запуска на Nintendo Switch.

## Изменения

### 1. Runtime exceptions и unwinding

- Runtime faults создают heap-объекты исключений, а не только возвращают диагностическую ошибку.
- Реализованы обработчики для `ArithmeticException`, `NullPointerException`, `ArrayIndexOutOfBoundsException`, `ArrayStoreException`, `ClassCastException` и `NegativeArraySizeException` в поддержанных инструкциях.
- `athrow null` преобразуется в `NullPointerException`.
- Исключение проходит через exception table текущего метода; если обработчика нет, interpreter снимает кадр и проверяет вызывающий метод по сохранённому `call_site_pc`.
- `call_site_pc` включён в checkpoint/restore.
- Добавлена базовая встроенная иерархия `RuntimeException -> Exception -> Throwable -> Object` для catch-проверок, даже если системные классы не загружены из JAR.

### 2. Проверка `aastore`

- Проверяется, что целевой массив действительно reference array.
- Проверяется валидность ссылки записываемого объекта.
- Проверяется совместимость runtime-класса объекта с component type массива.
- Несовместимая запись вызывает `ArrayStoreException`, которую можно перехватить в Java-коде.

### 3. ConstantValue и class initialization

- Class parser извлекает индекс `ConstantValue` у поля и валидирует тип constant-pool entry относительно field descriptor.
- Поддержаны значения Integer, Float, Long, Double и String.
- Перед первым `getstatic` / `putstatic` resumable interpreter инициализирует статические значения по умолчанию и ConstantValue.
- Если класс объявляет `<clinit>()V`, он запускается как отдельный resumable frame; после возврата исходная static-инструкция повторяется.
- Учитывается цепочка загруженных суперклассов: сначала выбирается ещё не инициализированный предок.
- Состояния initialized/initializing и маркер кадра `<clinit>` сохраняются в checkpoint.

## Проверки host

Прошли отдельные host test runners:

1. `run_jvm_resumable_object_interpreter_test.sh` — включая вложенный `ArithmeticException`, NPE, ArrayStoreException, bounds, ClassCastException, `<clinit>`, ConstantValue, checkpoint и scheduler.
2. `run_jvm_object_interpreter_test.sh`.
3. `run_jvm_class_runtime_test.sh`.
4. `run_jvm_execution_test.sh`.
5. `run_java_thread_execution_runtime_test.sh`.
6. `run_jvm_exception_runtime_test.sh`.
7. `run_bytecode_verifier_test.sh`.
8. `run_class_linker_test.sh`.

Все восемь runners отдельно завершались с PASS в ходе проверки. Повторный последовательный прогон в одной оболочке был остановлен ограничением времени после первых трёх PASS; этот прерванный общий прогон не засчитывается как полный PASS.

## Ограничения и честный статус

- Состояние class initialization пока хранится внутри экземпляра resumable interpreter, а не в общем VM-wide Class object; несколько независимых Java-thread interpreter instances ещё требуют общей синхронизации и единого состояния инициализации.
- Ошибка внутри `<clinit>` пока не реализует полностью спецификационную пару `ExceptionInInitializerError` / последующий `NoClassDefFoundError`.
- Runtime exception coverage ограничен перечисленными opcode paths; не все JVM-инструкции и все возможные VM errors реализованы.
- `aastore` использует доступную иерархию зарегистрированных классов плюс базовые core-типы; полная модель массивов JVM (covariance многомерных массивов, Cloneable/Serializable) отсутствует.
- `ConstantValue` поддержан на уровне parser и resumable runtime; старый синхронный Object Interpreter не переведён на эту реализацию.
- Старый Object Interpreter не заменён автоматически. Полный Minecraft JAR ещё не исполняется через этот runtime.
- Полный JNI/LWJGL/OpenGL, production-grade class library, NRO build и запуск на Switch не подтверждены.

## Следующий этап v138

1. VM-wide class initialization state с блокировкой/ожиданием между Java-потоками.
2. Ошибки `<clinit>`: `ExceptionInInitializerError`, erroneous class state и `NoClassDefFoundError`.
3. Продолжение opcode coverage по реальным классам Minecraft 1.2.5.
4. Сверка bytecode coverage с class inventory оригинального JAR и расширение original-JAR probe.
5. Начало реального переноса игровых классов на resumable object runtime; не заявлять совместимость, пока оригинальные методы не проходят проверку.
