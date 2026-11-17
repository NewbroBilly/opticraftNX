#include "switch_awt_host.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>

namespace opti {
int presents = 0;
bool platform_present_rgba8(const std::uint32_t*, int, int) { ++presents; return true; }
}

int main() {
    using namespace opti::runtime;
    SwitchAwtHost host;
    const auto cb = host.callbacks();
    assert(cb.user == &host && cb.create_window && cb.set_visible);
    assert(!cb.set_visible(cb.user, true));
    assert(!cb.create_window(cb.user, "Minecraft", 0, 480));
    assert(cb.create_window(cb.user, "Minecraft", 854, 480));
    assert(host.created() && !host.visible());
    assert(host.title() == "Minecraft" && host.width() == 854 && host.height() == 480);
    const std::uint32_t pixel = 0xffffffffu;
    assert(!host.present_rgba8(&pixel, 1, 1));
    assert(opti::presents == 0);
    assert(cb.set_visible(cb.user, true));
    assert(host.visible());
    assert(host.present_rgba8(&pixel, 1, 1));
    assert(opti::presents == 1);
    assert(cb.set_visible(cb.user, false));
    assert(!host.present_rgba8(&pixel, 1, 1));
    assert(opti::presents == 1);
    std::cout << "Switch AWT window state and framebuffer forwarding PASS\n";
}
