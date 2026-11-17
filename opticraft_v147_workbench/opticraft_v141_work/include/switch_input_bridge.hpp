#pragma once
#include "input_adapter.hpp"
#include <cstdint>
#include <vector>

namespace opti {
struct PackedInputEvent {
    std::int32_t type; // 1 key-down, 2 key-up, 3 mouse-down, 4 mouse-up, 5 mouse-motion
    std::int32_t code;
    std::int32_t x;
    std::int32_t y;
};
class SwitchInputBridge {
public:
    void update(const ControllerFrame& frame, int screen_width, int screen_height,
                int touch_x, int touch_y, bool touch_down);
    std::vector<PackedInputEvent> drain();
    bool key_down(int lwjgl_key) const;
    bool mouse_down(int button) const;
    int mouse_x() const { return mouse_x_; }
    int mouse_y() const { return mouse_y_; }
    void reset();
private:
    InputAdapter adapter_;
    std::vector<PackedInputEvent> pending_;
    std::uint32_t keys_ = 0;
    std::uint32_t mouse_ = 0;
    int mouse_x_ = 0, mouse_y_ = 0;
    bool touch_was_down_ = false;
    bool cursor_initialized_ = false;
};
}
