#pragma once
#include "jvm_object_interpreter.hpp"
#include <string>

namespace opti::runtime {

// AWT top-level window adapter for the Switch framebuffer backend.
// Java AWT calls update logical window state; pixel presentation is delegated
// to the existing libnx RGBA8 framebuffer path.
class SwitchAwtHost {
public:
    AwtHostCallbacks callbacks();
    bool present_rgba8(const std::uint32_t* pixels, int width, int height) const;

    bool created() const { return created_; }
    bool visible() const { return visible_; }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::string& title() const { return title_; }

private:
    static bool create_window(void* user, const char* title, int width, int height);
    static bool set_visible(void* user, bool visible);

    bool created_ = false;
    bool visible_ = false;
    int width_ = 0;
    int height_ = 0;
    std::string title_;
};

} // namespace opti::runtime
