#include "switch_awt_host.hpp"
namespace opti { bool platform_present_rgba8(const std::uint32_t* pixels, int width, int height); }

namespace opti::runtime {

AwtHostCallbacks SwitchAwtHost::callbacks() {
    return {this, &SwitchAwtHost::create_window, &SwitchAwtHost::set_visible};
}

bool SwitchAwtHost::create_window(void* user, const char* title, int width, int height) {
    auto* self = static_cast<SwitchAwtHost*>(user);
    if (!self || !title || width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return false;
    self->title_ = title;
    self->width_ = width;
    self->height_ = height;
    self->created_ = true;
    self->visible_ = false;
    return true;
}

bool SwitchAwtHost::set_visible(void* user, bool visible) {
    auto* self = static_cast<SwitchAwtHost*>(user);
    if (!self || !self->created_) return false;
    self->visible_ = visible;
    return true;
}

bool SwitchAwtHost::present_rgba8(const std::uint32_t* pixels, int width, int height) const {
    if (!created_ || !visible_) return false;
    return opti::platform_present_rgba8(pixels, width, height);
}

} // namespace opti::runtime
