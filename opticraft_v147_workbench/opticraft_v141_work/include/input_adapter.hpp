#pragma once
#include <cstdint>

namespace opti {
enum : int {
    KEY_ESCAPE = 1, KEY_TAB = 15, KEY_Q = 16, KEY_W = 17, KEY_E = 18,
    KEY_T = 20, KEY_C = 46, KEY_LCONTROL = 29, KEY_A = 30, KEY_S = 31,
    KEY_D = 32, KEY_LSHIFT = 42, KEY_SPACE = 57,
    KEY_F1 = 59, KEY_F3 = 61, KEY_F5 = 63,
    KEY_UP = 200, KEY_LEFT = 203, KEY_RIGHT = 205, KEY_DOWN = 208
};
enum : int { MOUSE_LEFT = 0, MOUSE_RIGHT = 1 };

// libnx HidNpadButton bit positions (standard Npad full-key state).
enum : std::uint64_t {
    PAD_A = 1ull << 0, PAD_B = 1ull << 1,
    PAD_X = 1ull << 2, PAD_Y = 1ull << 3,
    PAD_LSTICK = 1ull << 4, PAD_RSTICK = 1ull << 5,
    PAD_L = 1ull << 6, PAD_R = 1ull << 7,
    PAD_ZL = 1ull << 8, PAD_ZR = 1ull << 9,
    PAD_PLUS = 1ull << 10, PAD_MINUS = 1ull << 11,
    PAD_LEFT = 1ull << 12, PAD_UP = 1ull << 13,
    PAD_RIGHT = 1ull << 14, PAD_DOWN = 1ull << 15
};

struct ControllerFrame {
    std::uint64_t buttons;
    std::int32_t left_x, left_y;
    std::int32_t right_x, right_y;
};
struct InputEvent {
    enum Type { KeyDown, KeyUp, MouseDown, MouseUp, MouseWheel } type;
    int code;
};
struct EventBatch {
    InputEvent events[32];
    unsigned count;
};

class InputAdapter {
public:
    EventBatch update(const ControllerFrame& frame);
    void reset();
private:
    std::uint32_t previous_keys_ = 0;
    std::uint32_t previous_mouse_ = 0;
    std::uint64_t previous_buttons_ = 0;
};
}
