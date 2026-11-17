#pragma once
#include <cstdint>
using u64 = std::uint64_t;
struct PrintConsole {};
struct PadState {};
enum { HidNpadStyleSet_NpadStandard = 1 };
enum : u64 { HidNpadButton_A = 1ull << 0, HidNpadButton_Plus = 1ull << 10 };
inline PrintConsole* consoleInit(void*) { static PrintConsole c; return &c; }
inline void consoleClear() {}
inline void consoleUpdate(PrintConsole*) {}
inline void consoleExit(PrintConsole*) {}
inline void padConfigureInput(int, int) {}
inline void padInitializeDefault(PadState*) {}
inline void padUpdate(PadState*) {}
inline u64 padGetButtonsDown(PadState*) { return 0; }
inline bool appletMainLoop() { return false; }
