#pragma once
#include <switch.h>
#include <cstdint>
namespace opti {
struct InputSnapshot {
    u64 buttons;
    s32 left_x, left_y;
    s32 right_x, right_y;
    u64 pressed;
    u64 released;
    bool touch_down;
    s32 touch_x, touch_y;
};
bool platform_init();
void platform_shutdown();
InputSnapshot platform_poll_input();
bool platform_should_exit(const InputSnapshot& input);
void platform_present_diagnostic_frame(const InputSnapshot& input);
bool platform_present_rgba8(const std::uint32_t* pixels,int width,int height);
}
