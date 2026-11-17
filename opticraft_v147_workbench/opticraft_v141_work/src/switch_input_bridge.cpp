#include "switch_input_bridge.hpp"
#include <algorithm>

namespace opti {
namespace {
std::uint32_t key_mask(int key) {
    switch (key) {
        case KEY_W: return 1u<<0; case KEY_S: return 1u<<1;
        case KEY_A: return 1u<<2; case KEY_D: return 1u<<3;
        case KEY_LCONTROL: return 1u<<4; case KEY_LSHIFT: return 1u<<5;
        case KEY_SPACE: return 1u<<6; case KEY_Q: return 1u<<7;
        case KEY_E: return 1u<<8; case KEY_C: return 1u<<9;
        case KEY_ESCAPE: return 1u<<10; case KEY_TAB: return 1u<<11;
        case KEY_F1: return 1u<<12; case KEY_F3: return 1u<<13;
        case KEY_F5: return 1u<<14; case KEY_T: return 1u<<15;
        default: return 0;
    }
}
constexpr std::uint64_t B_TOUCH = 1ull << 16;
constexpr int DEADZONE = 8500;
constexpr int MOUSE_SPEED = 7;
}
void SwitchInputBridge::update(const ControllerFrame& frame, int sw, int sh,
                               int touch_x, int touch_y, bool touch_down) {
    if (!cursor_initialized_) {
        mouse_x_ = std::max(0, sw / 2);
        mouse_y_ = std::max(0, sh / 2);
        cursor_initialized_ = true;
    }
    EventBatch batch = adapter_.update(frame);
    for (unsigned i=0; i<batch.count; ++i) {
        const InputEvent& e = batch.events[i];
        const int type = e.type == InputEvent::KeyDown ? 1 :
                         e.type == InputEvent::KeyUp ? 2 :
                         e.type == InputEvent::MouseDown ? 3 :
                         e.type == InputEvent::MouseUp ? 4 : 6;
        pending_.push_back({type, e.code, mouse_x_, mouse_y_});
        if (e.type == InputEvent::KeyDown) {
            // State bitmap is maintained separately for LWJGL isKeyDown queries.
            keys_ |= key_mask(e.code);
        } else if (e.type == InputEvent::KeyUp) {
            keys_ &= ~key_mask(e.code);
        } else if (e.type == InputEvent::MouseDown) {
            mouse_ |= 1u << (e.code & 31);
        } else if (e.type == InputEvent::MouseUp) {
            mouse_ &= ~(1u << (e.code & 31));
        }
    }
    const int dx = (frame.right_x > DEADZONE || frame.right_x < -DEADZONE)
        ? frame.right_x * MOUSE_SPEED / 32767 : 0;
    const int dy = (frame.right_y > DEADZONE || frame.right_y < -DEADZONE)
        ? frame.right_y * MOUSE_SPEED / 32767 : 0;
    if (dx || dy) {
        const int old_x = mouse_x_, old_y = mouse_y_;
        mouse_x_ = std::clamp(mouse_x_ + dx, 0, std::max(0, sw - 1));
        mouse_y_ = std::clamp(mouse_y_ - dy, 0, std::max(0, sh - 1));
        if (old_x != mouse_x_ || old_y != mouse_y_)
            pending_.push_back({5, 0, mouse_x_, mouse_y_});
    }
    if (touch_down) {
        const int tx = std::clamp(touch_x * sw / 1280, 0, std::max(0, sw - 1));
        const int ty = std::clamp(touch_y * sh / 720, 0, std::max(0, sh - 1));
        if (tx != mouse_x_ || ty != mouse_y_) {
            mouse_x_ = tx; mouse_y_ = ty;
            pending_.push_back({5, 0, mouse_x_, mouse_y_});
        }
        if (!touch_was_down_) {
            mouse_ |= 1u;
            pending_.push_back({3, 0, mouse_x_, mouse_y_});
        }
    } else if (touch_was_down_) {
        mouse_ &= ~1u;
        pending_.push_back({4, 0, mouse_x_, mouse_y_});
    }
    touch_was_down_ = touch_down;
}
std::vector<PackedInputEvent> SwitchInputBridge::drain() {
    std::vector<PackedInputEvent> result;
    result.swap(pending_);
    return result;
}
bool SwitchInputBridge::key_down(int key) const {
    const std::uint32_t mask = key_mask(key);
    return mask != 0 && (keys_ & mask) != 0;
}
bool SwitchInputBridge::mouse_down(int button) const {
    return (mouse_ & (1u << (button & 31))) != 0;
}
void SwitchInputBridge::reset() {
    adapter_.reset(); pending_.clear(); keys_=0; mouse_=0; mouse_x_=mouse_y_=0; touch_was_down_=false; cursor_initialized_=false;
}
}
