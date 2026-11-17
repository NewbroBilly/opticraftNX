#pragma once
#include "class_code_parser.hpp"
#include "jvm_runtime.hpp"
#include "jvm_execution_frames.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace opti::runtime {
class JvmClassRuntime;
}
namespace opti::gl { class LwjglGL11Bridge; }
namespace opti::runtime {
struct AwtHostCallbacks {
    void* user = nullptr;
    bool (*create_window)(void*, const char*, int, int) = nullptr;
    bool (*set_visible)(void*, bool) = nullptr;
};

struct LwjglKeyboardEvent {
    int key = 0;
    int character = 0;
    bool state = false;
    std::uint64_t nanoseconds = 0;
};

struct LwjglMouseEvent {
    int button = -1;
    bool state = false;
    int x = 0;
    int y = 0;
    int wheel = 0;
};

// Platform input is polled by LWJGL from the original game loop. The callback
// provider owns controller state and pending event queues; the interpreter
// exposes those values through LWJGL 2 Mouse/Keyboard methods.
struct InputHostCallbacks {
    void* user = nullptr;
    void (*poll)(void*) = nullptr;
    bool (*key_down)(void*, int) = nullptr;
    bool (*mouse_down)(void*, int) = nullptr;
    bool (*next_keyboard_event)(void*, LwjglKeyboardEvent*) = nullptr;
    bool (*next_mouse_event)(void*, LwjglMouseEvent*) = nullptr;
    int (*mouse_x)(void*) = nullptr;
    int (*mouse_y)(void*) = nullptr;
    bool (*should_close)(void*) = nullptr;
};

struct ObjectExecutionResult {
    bool success = false;
    JvmValue value = NullValue{};
    std::string error;
    std::size_t instructions_executed = 0;
};

// Interpreter for the executable subset implemented by this project.
// When a class runtime is supplied, method calls and object layouts are resolved
// across registered classes instead of being limited to the current class.
class ObjectBytecodeInterpreter {
public:
    explicit ObjectBytecodeInterpreter(JvmHeap& heap, JvmStaticFields& statics,
                                       std::size_t budget = 1000000,
                                       JvmClassRuntime* classes = nullptr,
                                       AwtHostCallbacks* awt_host = nullptr,
                                       opti::gl::LwjglGL11Bridge* gl_bridge = nullptr,
                                       InputHostCallbacks* input_host = nullptr)
        : heap_(heap), statics_(statics), budget_(budget), classes_(classes), awt_host_(awt_host), gl_bridge_(gl_bridge), input_host_(input_host) {}

    const JvmExecutionFrameStack& execution_frames() const { return execution_frames_; }
    const std::string& diagnostics() const { return diagnostics_; }

    ObjectExecutionResult invoke_static(const ParsedClassCode& cls,
                                        const std::string& name,
                                        const std::string& descriptor,
                                        const std::vector<JvmValue>& args);

private:
    JvmHeap& heap_;
    JvmStaticFields& statics_;
    std::size_t budget_;
    JvmClassRuntime* classes_;
    AwtHostCallbacks* awt_host_;
    opti::gl::LwjglGL11Bridge* gl_bridge_;
    InputHostCallbacks* input_host_;
    LwjglKeyboardEvent current_keyboard_event_{};
    LwjglMouseEvent current_mouse_event_{};
    int previous_mouse_x_ = 0;
    int previous_mouse_y_ = 0;
    bool mouse_grabbed_ = false;
    ObjectRef current_thread_ = 0;
    std::vector<std::string> texture_resource_paths_;
    JvmExecutionFrameStack execution_frames_;
    std::string diagnostics_;
};
}
