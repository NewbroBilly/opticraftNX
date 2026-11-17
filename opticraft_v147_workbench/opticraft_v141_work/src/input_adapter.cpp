#include "input_adapter.hpp"

namespace opti {
namespace {
constexpr std::int32_t DEADZONE = 10000;
enum KeySlot {
    W, S, A, D, SPRINT, SNEAK, JUMP, DROP, INVENTORY, CRAFT,
    PAUSE, PLAYER_LIST, HIDE_GUI, DEBUG_INFO, PERSPECTIVE, CHAT,
    KEY_COUNT
};
constexpr int KEY_CODES[KEY_COUNT] = {
    KEY_W, KEY_S, KEY_A, KEY_D, KEY_LCONTROL, KEY_LSHIFT, KEY_SPACE,
    KEY_Q, KEY_E, KEY_C, KEY_ESCAPE, KEY_TAB, KEY_F1, KEY_F3, KEY_F5, KEY_T
};
void append(EventBatch& batch, InputEvent::Type type, int code) {
    if (batch.count < sizeof(batch.events) / sizeof(batch.events[0]))
        batch.events[batch.count++] = {type, code};
}
}

EventBatch InputAdapter::update(const ControllerFrame& f) {
    std::uint32_t keys = 0, mouse = 0;
    const std::uint64_t b = f.buttons;

    // Left stick: movement; L3 is sprint. Right-stick click is F5.
    if (f.left_y > DEADZONE) keys |= 1u << W;
    if (f.left_y < -DEADZONE) keys |= 1u << S;
    if (f.left_x < -DEADZONE) keys |= 1u << A;
    if (f.left_x > DEADZONE) keys |= 1u << D;
    if (b & PAD_LSTICK) keys |= 1u << SPRINT;
    if (b & PAD_RSTICK) keys |= 1u << PERSPECTIVE;

    // Face buttons and system buttons.
    if (b & PAD_A) { keys |= 1u << JUMP; mouse |= 1u << MOUSE_LEFT; }
    if (b & PAD_B) keys |= 1u << DROP;
    if (b & PAD_X) keys |= 1u << INVENTORY;
    if (b & PAD_Y) keys |= 1u << CRAFT; // Custom C shortcut; vanilla 1.2.5 has no craft-menu key.
    if (b & PAD_PLUS) keys |= 1u << PAUSE;
    if (b & PAD_MINUS) keys |= 1u << PLAYER_LIST;

    // D-pad: F1, sneak, F3, chat.
    if (b & PAD_UP) keys |= 1u << HIDE_GUI;
    if (b & PAD_DOWN) keys |= 1u << SNEAK;
    if (b & PAD_LEFT) keys |= 1u << DEBUG_INFO;
    if (b & PAD_RIGHT) keys |= 1u << CHAT;

    // ZR attacks/breaks (left mouse); ZL uses/places (right mouse).
    if (b & PAD_ZR) mouse |= 1u << MOUSE_LEFT;
    if (b & PAD_ZL) mouse |= 1u << MOUSE_RIGHT;

    EventBatch batch{};
    for (int slot = 0; slot < KEY_COUNT; ++slot) {
        const std::uint32_t mask = 1u << slot;
        if ((keys & mask) && !(previous_keys_ & mask))
            append(batch, InputEvent::KeyDown, KEY_CODES[slot]);
        if (!(keys & mask) && (previous_keys_ & mask))
            append(batch, InputEvent::KeyUp, KEY_CODES[slot]);
    }
    for (int button = 0; button < 2; ++button) {
        const std::uint32_t mask = 1u << button;
        if ((mouse & mask) && !(previous_mouse_ & mask))
            append(batch, InputEvent::MouseDown, button);
        if (!(mouse & mask) && (previous_mouse_ & mask))
            append(batch, InputEvent::MouseUp, button);
    }

    // L/R shoulder buttons generate standard LWJGL wheel ticks for hotbar selection.
    const bool l_now = (b & PAD_L) != 0;
    const bool r_now = (b & PAD_R) != 0;
    const bool l_before = (previous_buttons_ & PAD_L) != 0;
    const bool r_before = (previous_buttons_ & PAD_R) != 0;
    if (l_now && !l_before) append(batch, InputEvent::MouseWheel, 120);
    if (r_now && !r_before) append(batch, InputEvent::MouseWheel, -120);

    previous_keys_ = keys;
    previous_mouse_ = mouse;
    previous_buttons_ = b;
    return batch;
}

void InputAdapter::reset() {
    previous_keys_ = 0;
    previous_mouse_ = 0;
    previous_buttons_ = 0;
}
}
