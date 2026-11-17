#include "input_adapter.hpp"
#include <cassert>

using namespace opti;

static bool has(const EventBatch& batch, InputEvent::Type type, int code) {
    for (unsigned i=0; i<batch.count; ++i)
        if (batch.events[i].type == type && batch.events[i].code == code) return true;
    return false;
}

int main() {
    InputAdapter adapter;
    ControllerFrame f{};
    assert(adapter.update(f).count == 0);

    // Left stick movement and L3 sprint.
    f.left_y = 20000;
    f.buttons = PAD_LSTICK;
    auto movement = adapter.update(f);
    assert(has(movement, InputEvent::KeyDown, KEY_W));
    assert(has(movement, InputEvent::KeyDown, KEY_LCONTROL));
    assert(adapter.update(f).count == 0); // no repeated key-down while held
    f.left_y = 0; f.buttons = 0;
    auto release = adapter.update(f);
    assert(has(release, InputEvent::KeyUp, KEY_W));
    assert(has(release, InputEvent::KeyUp, KEY_LCONTROL));

    // Face/system buttons.
    f.buttons = PAD_A|PAD_B|PAD_X|PAD_Y|PAD_PLUS|PAD_MINUS;
    auto face = adapter.update(f);
    assert(has(face, InputEvent::KeyDown, KEY_SPACE));
    assert(has(face, InputEvent::MouseDown, MOUSE_LEFT));
    assert(has(face, InputEvent::KeyDown, KEY_Q));
    assert(has(face, InputEvent::KeyDown, KEY_E));
    assert(has(face, InputEvent::KeyDown, KEY_C));
    assert(has(face, InputEvent::KeyDown, KEY_ESCAPE));
    assert(has(face, InputEvent::KeyDown, KEY_TAB));

    // D-pad shortcuts and right-stick click perspective toggle.
    f.buttons = PAD_UP|PAD_DOWN|PAD_LEFT|PAD_RIGHT|PAD_RSTICK;
    auto dpad = adapter.update(f);
    assert(has(dpad, InputEvent::KeyDown, KEY_F1));
    assert(has(dpad, InputEvent::KeyDown, KEY_LSHIFT));
    assert(has(dpad, InputEvent::KeyDown, KEY_F3));
    assert(has(dpad, InputEvent::KeyDown, KEY_T));
    assert(has(dpad, InputEvent::KeyDown, KEY_F5));

    // ZR attacks (left mouse), ZL uses (right mouse).
    f.buttons = PAD_ZR|PAD_ZL;
    auto actions = adapter.update(f);
    assert(has(actions, InputEvent::MouseDown, MOUSE_LEFT));
    assert(has(actions, InputEvent::MouseDown, MOUSE_RIGHT));

    // L/R shoulders generate one hotbar wheel tick per press, not per held frame.
    f.buttons = PAD_L|PAD_R;
    auto wheel = adapter.update(f);
    assert(has(wheel, InputEvent::MouseWheel, 120));
    assert(has(wheel, InputEvent::MouseWheel, -120));
    assert(adapter.update(f).count == 0);
    return 0;
}
