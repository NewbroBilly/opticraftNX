# OptiCraft Switch — v135

Продолжение инженерной базы v134. В этой версии добавлен ограниченный resumable object interpreter на `JvmValue`/`ObjectRef`, использующий существующие heap и class resolver, а также подключённый к cooperative Java thread scheduler.

См. `REPORT_v135_RESUMABLE_OBJECT_RUNTIME_RU.md` и `VALIDATION_v135.txt`.

Важно: это не полная JVM и не готовый запуск Minecraft. Старый `ObjectBytecodeInterpreter` пока не заменён; NRO для Switch в этой версии не собирался.
