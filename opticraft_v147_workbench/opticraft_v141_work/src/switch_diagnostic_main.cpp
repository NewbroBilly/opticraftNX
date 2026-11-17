#include <switch.h>
#include "switch_backend.hpp"
#include "framebuffer_blit.hpp"
#include <cstdint>
#include <cstring>

namespace opti {

static Framebuffer g_fb{};
static bool g_ready = false;
static HidNpadFullKeyState g_state{};
static u64 g_previous_buttons = 0;


static bool read_npad(HidNpadFullKeyState *out) {
    // Handheld mode first; fall back to player 1 for docked controllers.
    size_t count = hidGetNpadStatesHandheld(HidNpadIdType_Handheld, out, 1);
    if (count == 0)
        count = hidGetNpadStatesFullKey(HidNpadIdType_No1, out, 1);
    return count != 0;
}

bool platform_init() {
    Result rc = hidInitialize();
    if (R_FAILED(rc)) return false;

    hidInitializeNpad();
    hidInitializeTouchScreen();

    rc = framebufferCreate(&g_fb, nwindowGetDefault(),
                           1280, 720, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) {
        hidExit();
        return false;
    }

    rc = framebufferMakeLinear(&g_fb);
    if (R_FAILED(rc)) {
        framebufferClose(&g_fb);
        hidExit();
        return false;
    }

    g_ready = true;
    return true;
}

void platform_shutdown() {
    if (!g_ready) return;
    framebufferClose(&g_fb);
    hidExit();
    g_ready = false;
}

InputSnapshot platform_poll_input() {
    InputSnapshot result{};
    if (!read_npad(&g_state)) return result;

    result.buttons = g_state.buttons;
    result.left_x = g_state.analog_stick_l.x;
    result.left_y = g_state.analog_stick_l.y;
    result.right_x = g_state.analog_stick_r.x;
    result.right_y = g_state.analog_stick_r.y;
    result.pressed = result.buttons & ~g_previous_buttons;
    result.released = g_previous_buttons & ~result.buttons;
    g_previous_buttons = result.buttons;

    HidTouchScreenState touch{};
    if (hidGetTouchScreenStates(&touch, 1) > 0 && touch.count > 0) {
        result.touch_down = true;
        result.touch_x = static_cast<s32>(touch.touches[0].x);
        result.touch_y = static_cast<s32>(touch.touches[0].y);
    }
    return result;
}

// JNI-facing frame hook: reads the current libnx controller/touch snapshot.
// The Java LWJGL adapter calls this once per game frame.
extern "C" void opti_switch_read_input(std::uint64_t* buttons,
                                        std::int32_t* left_x, std::int32_t* left_y,
                                        std::int32_t* right_x, std::int32_t* right_y,
                                        std::int32_t* touch_x, std::int32_t* touch_y,
                                        bool* touch_down) {
    const InputSnapshot snapshot = platform_poll_input();
    if (buttons) *buttons = snapshot.buttons;
    if (left_x) *left_x = snapshot.left_x;
    if (left_y) *left_y = snapshot.left_y;
    if (right_x) *right_x = snapshot.right_x;
    if (right_y) *right_y = snapshot.right_y;
    if (touch_x) *touch_x = snapshot.touch_x;
    if (touch_y) *touch_y = snapshot.touch_y;
    if (touch_down) *touch_down = snapshot.touch_down;
}

bool platform_should_exit(const InputSnapshot& input) {
    return (input.pressed & HidNpadButton_Plus) != 0;
}

static inline void put_pixel(u32 *row, unsigned x, u32 color) {
    row[x] = color;
}

static void draw_marker(u32 *pixels, u32 stride, int cx, int cy,
                        int radius, u32 color) {
    for (int y = -radius; y <= radius; ++y) {
        const int py = cy + y;
        if (py < 0 || py >= 720) continue;
        auto *row = reinterpret_cast<u32*>(
            reinterpret_cast<u8*>(pixels) + py * stride);
        for (int x = -radius; x <= radius; ++x) {
            const int px = cx + x;
            if (px >= 0 && px < 1280 && x*x + y*y <= radius*radius)
                put_pixel(row, static_cast<unsigned>(px), color);
        }
    }
}

// Presentation bridge for the CPU compatibility renderer's RGBA8 framebuffer.
bool platform_present_rgba8(const std::uint32_t* pixels,int width,int height) {
    if (!g_ready || pixels == nullptr || width <= 0 || height <= 0) return false;
    u32 stride = 0;
    auto *destination = static_cast<u8*>(framebufferBegin(&g_fb, &stride));
    if (!destination) return false;
    const bool ok = blit_rgba8(destination, stride, 1280, 720, pixels, width, height);
    framebufferEnd(&g_fb);
    return ok;
}

// Diagnostic visualization only: no Minecraft rendering or OpenGL context.
void platform_present_diagnostic_frame(const InputSnapshot& input) {
    if (!g_ready) return;

    u32 stride = 0;
    auto *pixels = static_cast<u32*>(framebufferBegin(&g_fb, &stride));
    if (!pixels) return;

    for (unsigned y = 0; y < 720; ++y) {
        auto *row = reinterpret_cast<u32*>(
            reinterpret_cast<u8*>(pixels) + y * stride);
        for (unsigned x = 0; x < 1280; ++x) {
            const u8 r = static_cast<u8>((x * 255) / 1279);
            const u8 g = static_cast<u8>((y * 255) / 719);
            row[x] = RGBA8(r, g, 0x30, 0xFF);
        }
    }

    // Two stick indicators. Scale conservatively without assuming a fixed
    // calibration range beyond the signed 32-bit libnx representation.
    const int lx = 260 + static_cast<int>((static_cast<s64>(input.left_x) * 90) / 32768);
    const int ly = 360 - static_cast<int>((static_cast<s64>(input.left_y) * 90) / 32768);
    const int rx = 1020 + static_cast<int>((static_cast<s64>(input.right_x) * 90) / 32768);
    const int ry = 360 - static_cast<int>((static_cast<s64>(input.right_y) * 90) / 32768);
    draw_marker(pixels, stride, 260, 360, 105, RGBA8(20,20,20,255));
    draw_marker(pixels, stride, 1020, 360, 105, RGBA8(20,20,20,255));
    draw_marker(pixels, stride, lx, ly, 22, RGBA8(255,255,255,255));
    draw_marker(pixels, stride, rx, ry, 22, RGBA8(255,255,255,255));

    const struct { u64 mask; int x; int y; } indicators[] = {
        {HidNpadButton_A, 640, 260}, {HidNpadButton_B, 700, 320},
        {HidNpadButton_X, 580, 320}, {HidNpadButton_Y, 640, 380},
        {HidNpadButton_L, 500, 120}, {HidNpadButton_R, 780, 120},
        {HidNpadButton_Plus, 640, 520}, {HidNpadButton_Minus, 640, 580}
    };
    for (const auto& item : indicators) {
        const u32 color = (input.buttons & item.mask)
            ? RGBA8(255,230,40,255) : RGBA8(35,35,35,255);
        draw_marker(pixels, stride, item.x, item.y, 18, color);
    }

    framebufferEnd(&g_fb);
}

} // namespace opti

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!opti::platform_init()) return 1;

    while (appletMainLoop()) {
        const auto input = opti::platform_poll_input();
        if (opti::platform_should_exit(input)) break;
        opti::platform_present_diagnostic_frame(input);
    }

    opti::platform_shutdown();
    return 0;
}
