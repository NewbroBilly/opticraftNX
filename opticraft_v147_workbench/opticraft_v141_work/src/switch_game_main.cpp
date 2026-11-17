#include <switch.h>
#include "java_runtime_bootstrap.hpp"
#include "jvm_class_runtime.hpp"
#include "jvm_object_interpreter.hpp"
#include "lwjgl_gl11_bridge.hpp"
#include "switch_awt_host.hpp"
#include "switch_interpreter_input.hpp"
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace opti {
bool platform_init();
void platform_shutdown();
bool platform_present_rgba8(const std::uint32_t* pixels, int width, int height);
}

namespace {
constexpr const char* kDataRoot = "sdmc:/switch/opticraft";
constexpr std::size_t kMaxArchiveBytes = 128u * 1024u * 1024u;

void log_line(std::ofstream& log, const std::string& line) {
    if (log) { log << line << '\n'; log.flush(); }
}

bool register_archive(const std::string& path, bool game_archive,
                      opti::runtime::JvmClassRuntime& classes,
                      std::ofstream& log) {
    using namespace opti::runtime;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { log_line(log, "OPEN_FAILED " + path); return false; }
    const std::streamoff end = input.tellg();
    if (end < 22 || static_cast<std::uint64_t>(end) > kMaxArchiveBytes) {
        log_line(log, "ARCHIVE_SIZE_INVALID " + path);
        return false;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), end)) {
        log_line(log, "ARCHIVE_READ_FAILED " + path);
        return false;
    }
    const JarInspection inspection = inspect_jar(bytes.data(), bytes.size(), game_archive);
    if (!inspection.valid) {
        log_line(log, "ARCHIVE_INVALID " + path + " " + inspection.error);
        return false;
    }
    std::size_t classes_added = 0;
    std::size_t resources_added = 0;
    for (const auto& entry : inspection.entries) {
        std::vector<std::uint8_t> content;
        std::string error;
        if (!extract_jar_entry(bytes.data(), bytes.size(), entry, content, &error)) {
            log_line(log, "ENTRY_EXTRACT_FAILED " + entry.name + " " + error);
            return false;
        }
        if (entry.name.size() >= 6 &&
            entry.name.compare(entry.name.size() - 6, 6, ".class") == 0) {
            ParsedClassCode parsed = parse_class_code(content.data(), content.size());
            if (!parsed.valid) {
                log_line(log, "CLASS_PARSE_FAILED " + entry.name + " " + parsed.error);
                return false;
            }
            if (classes.find_class(parsed.this_class)) continue;
            const auto registered = classes.register_class(std::move(parsed));
            if (!registered.success) {
                log_line(log, "CLASS_REGISTER_FAILED " + entry.name + " " + registered.error);
                return false;
            }
            ++classes_added;
        } else if (!entry.name.empty() && entry.name.back() != '/') {
            if (classes.register_resource(entry.name, std::move(content))) ++resources_added;
        }
    }
    log_line(log, "ARCHIVE_OK " + path + " classes=" +
             std::to_string(classes_added) + " resources=" +
             std::to_string(resources_added));
    return true;
}

bool validate_data_layout(std::ofstream& log) {
    const std::pair<const char*, bool> required[] = {
        {"minecraft.jar", true},
        {"lwjgl.jar", false},
        {"lwjgl_util.jar", false},
        {"jinput.jar", false},
        {"runtime/lib/rt.jar", false}
    };
    bool valid = true;
    for (const auto& item : required) {
        const std::string path = std::string(kDataRoot) + "/" + item.first;
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) {
            log_line(log, std::string("DATA_MISSING ") + path);
            valid = false;
            continue;
        }
        const std::streamoff size = input.tellg();
        if (size < 22 || static_cast<std::uint64_t>(size) > kMaxArchiveBytes) {
            log_line(log, std::string("DATA_SIZE_INVALID ") + path + " bytes=" +
                     std::to_string(size < 0 ? 0 : size));
            valid = false;
            continue;
        }
        log_line(log, std::string("DATA_READY ") + path + " bytes=" +
                 std::to_string(size));
    }
    return valid;
}

bool load_runtime(opti::runtime::JvmClassRuntime& classes, std::ofstream& log) {
    const std::string root = kDataRoot;
    const std::pair<std::string, bool> archives[] = {
        {root + "/minecraft.jar", true},
        {root + "/lwjgl.jar", false},
        {root + "/lwjgl_util.jar", false},
        {root + "/jinput.jar", false},
        {root + "/runtime/lib/rt.jar", false}
    };
    for (const auto& archive : archives) {
        if (!register_archive(archive.first, archive.second, classes, log)) return false;
    }
    return classes.find_class("net/minecraft/client/Minecraft") != nullptr;
}
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (!opti::platform_init()) return 1;

    const Result mount_result = fsdevMountSdmc();
    if (R_FAILED(mount_result)) {
        opti::platform_shutdown();
        return 2;
    }
    std::ofstream log(std::string(kDataRoot) + "/launch.log",
                      std::ios::out | std::ios::trunc);
    log_line(log, "OptiCraft original client boot");
    log_line(log, "DATA_ROOT " + std::string(kDataRoot));

    int exit_code = 0;
    opti::runtime::JvmClassRuntime classes;
    if (!validate_data_layout(log)) {
        log_line(log, "RUNTIME_LAYOUT_FAILED");
        exit_code = 3;
    } else if (!load_runtime(classes, log)) {
        log_line(log, "RUNTIME_LOAD_FAILED");
        exit_code = 4;
    } else {
        opti::runtime::JvmHeap heap;
        opti::runtime::JvmStaticFields statics;
        const auto home = heap.allocate_string(kDataRoot);
        if (!home || !statics.define("opti/switchport/SystemProperties", "user.home", home)) {
            log_line(log, "USER_HOME_SETUP_FAILED");
            exit_code = 5;
        } else {
            opti::runtime::SwitchAwtHost awt;
            auto awt_callbacks = awt.callbacks();
            opti::runtime::SwitchInterpreterInput input;
            auto input_callbacks = input.callbacks();
            opti::gl::LwjglGL11Bridge gl_bridge(854, 480);
            gl_bridge.set_present_callback([](const std::uint32_t* pixels,
                                               int width, int height) {
                opti::platform_present_rgba8(pixels, width, height);
            });
            opti::runtime::ObjectBytecodeInterpreter interpreter(
                heap, statics, std::numeric_limits<std::size_t>::max(),
                &classes, &awt_callbacks,
                &gl_bridge, &input_callbacks);
            const auto* main_class = classes.find_class("net/minecraft/client/Minecraft");
            const opti::runtime::ObjectRef args = heap.allocate_reference_array(0, "Ljava/lang/String;");
            log_line(log, "MINECRAFT_MAIN_BEGIN");
            const auto execution = main_class && args
                ? interpreter.invoke_static(*main_class, "main",
                    "([Ljava/lang/String;)V", {args})
                : opti::runtime::ObjectExecutionResult{};
            if (!execution.success) {
                log_line(log, "MINECRAFT_MAIN_STOPPED " + execution.error);
                for (const auto& frame : interpreter.execution_frames().frames()) {
                    log_line(log, "FRAME " + frame.class_name + "." +
                             frame.method_name + frame.descriptor + " pc=" +
                             std::to_string(frame.pc));
                }
                exit_code = 6;
            } else {
                log_line(log, "MINECRAFT_MAIN_RETURNED");
            }
        }
    }

    log_line(log, "OptiCraft original client shutdown");
    log.close();
    fsdevUnmountDevice("sdmc");
    opti::platform_shutdown();
    return exit_code;
}
