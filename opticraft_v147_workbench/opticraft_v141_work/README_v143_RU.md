# OptiCraft Switch v143

В этой контрольной точке добавлена и протестирована host-side раскладка контроллера Switch для LWJGL 2. Подробная таблица, тесты и ограничения находятся в `REPORT_v143_SWITCH_CONTROLLER_MAPPING_RU.md`.

Проверить адаптер:

```bash
mkdir -p tests/build/input_adapter
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude src/input_adapter.cpp tests/input_adapter_test.cpp -o tests/build/input_adapter/input_adapter_test
tests/build/input_adapter/input_adapter_test
LWJGL_JAR="/path/to/original/lwjgl.jar" tests/run_input_implementation_test.sh
tests/run_input_jni_test.sh
tests/run_switch_awt_host_test.sh
tests/run_platform_syntax_test.sh
```

Это не готовый NRO и не подтверждение работы контроллера на консоли. `SwitchInputImplementation.pump()` ещё требуется подключить к реальному игровому циклу Horizon.
