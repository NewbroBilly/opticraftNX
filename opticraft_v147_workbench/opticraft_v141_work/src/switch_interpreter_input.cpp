#include "switch_interpreter_input.hpp"
#include "switch_backend.hpp"
#include <chrono>

namespace opti::runtime {

InputHostCallbacks SwitchInterpreterInput::callbacks() {
    return {this, &SwitchInterpreterInput::poll,
            &SwitchInterpreterInput::key_down,
            &SwitchInterpreterInput::mouse_down,
            &SwitchInterpreterInput::next_keyboard_event,
            &SwitchInterpreterInput::next_mouse_event,
            &SwitchInterpreterInput::mouse_x,
            &SwitchInterpreterInput::mouse_y,
            &SwitchInterpreterInput::should_close};
}

void SwitchInterpreterInput::poll(void* user) {
    auto* self = static_cast<SwitchInterpreterInput*>(user);
    if (!self) return;
    const opti::InputSnapshot snapshot = opti::platform_poll_input();
    const opti::ControllerFrame frame{snapshot.buttons,
        snapshot.left_x, snapshot.left_y, snapshot.right_x, snapshot.right_y};
    self->bridge_.update(frame, 854, 480, snapshot.touch_x, snapshot.touch_y,
                         snapshot.touch_down);
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    for (const auto& packed : self->bridge_.drain()) {
        if (packed.type == 1 || packed.type == 2) {
            self->keyboard_events_.push_back({packed.code, 0, packed.type == 1, now});
        } else {
            LwjglMouseEvent event{};
            event.x = packed.x;
            event.y = packed.y;
            if (packed.type == 3 || packed.type == 4) {
                event.button = packed.code;
                event.state = packed.type == 3;
            } else if (packed.type == 6) {
                event.wheel = packed.code;
            }
            self->mouse_events_.push_back(event);
        }
    }
}

bool SwitchInterpreterInput::key_down(void* user, int key) {
    auto* self = static_cast<SwitchInterpreterInput*>(user);
    return self && self->bridge_.key_down(key);
}

bool SwitchInterpreterInput::mouse_down(void* user, int button) {
    auto* self = static_cast<SwitchInterpreterInput*>(user);
    return self && self->bridge_.mouse_down(button);
}

bool SwitchInterpreterInput::next_keyboard_event(void* user,
                                                  LwjglKeyboardEvent* event) {
    auto* self = static_cast<SwitchInterpreterInput*>(user);
    if (!self || !event || self->keyboard_events_.empty()) return false;
    *event = self->keyboard_events_.front();
    self->keyboard_events_.pop_front();
    return true;
}

bool SwitchInterpreterInput::next_mouse_event(void* user,
                                               LwjglMouseEvent* event) {
    auto* self = static_cast<SwitchInterpreterInput*>(user);
    if (!self || !event || self->mouse_events_.empty()) return false;
    *event = self->mouse_events_.front();
    self->mouse_events_.pop_front();
    return true;
}

int SwitchInterpreterInput::mouse_x(void* user) {
    auto* self = static_cast<SwitchInterpreterInput*>(user);
    return self ? self->bridge_.mouse_x() : 0;
}

int SwitchInterpreterInput::mouse_y(void* user) {
    auto* self = static_cast<SwitchInterpreterInput*>(user);
    return self ? self->bridge_.mouse_y() : 0;
}

bool SwitchInterpreterInput::should_close(void*) {
    return !appletMainLoop();
}

} // namespace opti::runtime
