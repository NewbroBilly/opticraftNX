#include <switch.h>
#include "switch_backend.hpp"
#include "framebuffer_blit.hpp"

namespace opti {
namespace {
Framebuffer framebuffer{};
bool ready = false;
HidNpadFullKeyState pad_state{};
std::uint64_t previous_buttons = 0;
constexpr int kScreenWidth = 1280;
constexpr int kScreenHeight = 720;
}

bool platform_init() {
    Result result = hidInitialize();
    if (R_FAILED(result)) return false;
    hidInitializeNpad();
    hidInitializeTouchScreen();
    const HidNpadIdType ids[] = {HidNpadIdType_Handheld, HidNpadIdType_No1};
    hidSetSupportedNpadIdType(ids, sizeof(ids) / sizeof(ids[0]));
    hidSetSupportedNpadStyleSet(HidNpadStyleTag_NpadHandheld |
                                 HidNpadStyleTag_NpadFullKey |
                                 HidNpadStyleTag_NpadJoyDual);

    result = framebufferCreate(&framebuffer, nwindowGetDefault(),
                               kScreenWidth, kScreenHeight,
                               PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(result)) { hidExit(); return false; }
    result = framebufferMakeLinear(&framebuffer);
    if (R_FAILED(result)) {
        framebufferClose(&framebuffer);
        hidExit();
        return false;
    }
    ready = true;
    previous_buttons = 0;
    return true;
}

void platform_shutdown() {
    if (!ready) return;
    framebufferClose(&framebuffer);
    hidExit();
    ready = false;
}

InputSnapshot platform_poll_input() {
    InputSnapshot snapshot{};
    size_t count = hidGetNpadStatesHandheld(HidNpadIdType_Handheld,
                                             &pad_state, 1);
    if (!count) count = hidGetNpadStatesFullKey(HidNpadIdType_No1,
                                                &pad_state, 1);
    if (!count) return snapshot;
    snapshot.buttons = pad_state.buttons;
    snapshot.left_x = pad_state.analog_stick_l.x;
    snapshot.left_y = pad_state.analog_stick_l.y;
    snapshot.right_x = pad_state.analog_stick_r.x;
    snapshot.right_y = pad_state.analog_stick_r.y;
    snapshot.pressed = snapshot.buttons & ~previous_buttons;
    snapshot.released = previous_buttons & ~snapshot.buttons;
    previous_buttons = snapshot.buttons;

    HidTouchScreenState touch{};
    if (hidGetTouchScreenStates(&touch, 1) > 0 && touch.count > 0) {
        snapshot.touch_down = true;
        snapshot.touch_x = touch.touches[0].x;
        snapshot.touch_y = touch.touches[0].y;
    }
    return snapshot;
}

bool platform_should_exit(const InputSnapshot& snapshot) {
    return (snapshot.pressed & HidNpadButton_Plus) != 0 || !appletMainLoop();
}

bool platform_present_rgba8(const std::uint32_t* pixels, int width, int height) {
    if (!ready || !pixels || width <= 0 || height <= 0) return false;
    u32 stride = 0;
    auto* destination = static_cast<u8*>(framebufferBegin(&framebuffer, &stride));
    if (!destination) return false;
    const bool ok = blit_rgba8(destination, stride, kScreenWidth, kScreenHeight,
                               pixels, width, height);
    framebufferEnd(&framebuffer);
    return ok;
}

} // namespace opti
