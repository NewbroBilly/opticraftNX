#pragma once
#include "jvm_object_interpreter.hpp"
#include "switch_input_bridge.hpp"
#include <deque>

namespace opti::runtime {

// Adapts libnx controller/touch snapshots to the LWJGL 2 input callbacks used
// by the original Java client interpreter.
class SwitchInterpreterInput {
public:
    InputHostCallbacks callbacks();

private:
    static void poll(void* user);
    static bool key_down(void* user, int key);
    static bool mouse_down(void* user, int button);
    static bool next_keyboard_event(void* user, LwjglKeyboardEvent* event);
    static bool next_mouse_event(void* user, LwjglMouseEvent* event);
    static int mouse_x(void* user);
    static int mouse_y(void* user);
    static bool should_close(void* user);

    opti::SwitchInputBridge bridge_;
    std::deque<LwjglKeyboardEvent> keyboard_events_;
    std::deque<LwjglMouseEvent> mouse_events_;
};

} // namespace opti::runtime
