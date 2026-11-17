// Keep the known-good hardware diagnostic target available. A separate build
// option runs the SD-card Java archive/classfile loading gate.
#if defined(OPTICRAFT_SWITCH_GAME)
#include "../src/switch_game_main.cpp"
#elif defined(OPTICRAFT_SWITCH_JAR_LOADER)
#include "../src/switch_jvm_loader_main.cpp"
#else
#include "../src/switch_diagnostic_main.cpp"
#endif
