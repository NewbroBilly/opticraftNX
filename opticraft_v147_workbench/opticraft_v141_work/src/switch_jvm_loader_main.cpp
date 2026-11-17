#include <switch.h>
#include "opticraft_jar_classloader.h"
#include <cstdint>
#include <cstdio>

namespace {
constexpr const char *kMinecraftJar = "sdmc:/switch/opticraft/minecraft.jar";
constexpr std::size_t kMaxJarBytes = 128u * 1024u * 1024u;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    consoleInit(nullptr);
    std::printf("OptiCraft JVM loader stage\n");
    std::printf("Reading %s\n\n", kMinecraftJar);

    const Result mount_result = fsdevMountSdmc();
    if (R_FAILED(mount_result)) {
        std::printf("SD mount failed: 0x%08X\n", static_cast<unsigned>(mount_result));
        std::printf("Place the original minecraft.jar at the path above.\n");
    } else {
        OptiJar *jar = nullptr;
        if (!opticraft_jar_open_file(kMinecraftJar, kMaxJarBytes, &jar)) {
            std::printf("JAR open failed. Check the file and SD path.\n");
        } else {
            OptiLoadedClass *minecraft = nullptr;
            if (!opticraft_jar_load_class(jar, "net/minecraft/client/Minecraft",
                                          &minecraft)) {
                std::printf("Minecraft.class load failed:\n%s\n",
                            opticraft_jar_last_error(jar));
            } else {
                OptiClassSummary summary{};
                uint16_t access = 0;
                uint32_t code_length = 0;
                const int has_main = opticraft_loaded_class_find_method(
                    minecraft, "main", "([Ljava/lang/String;)V", &access,
                    &code_length);
                const bool valid = opticraft_loaded_class_summary(minecraft,
                                                                   &summary) &&
                                   has_main && (access & 0x0008u) != 0 &&
                                   code_length != 0;
                std::printf("Minecraft.class: %s\n", valid ? "LOADED" : "INVALID");
                std::printf("Class version: %u.%u\n", summary.major_version,
                            summary.minor_version);
                std::printf("main(String[]): %s\n",
                            has_main ? "FOUND" : "MISSING");
                if (has_main) {
                    std::printf("main bytecode: %u bytes\n", code_length);
                    std::printf("main is static: %s\n",
                                (access & 0x0008u) != 0 ? "yes" : "no");
                }
                std::printf("\nThis stage loads the original class; it does not execute Java bytecode yet.\n");
                opticraft_loaded_class_destroy(minecraft);
            }
            opticraft_jar_close(jar);
        }
        fsdevUnmountDevice("sdmc");
    }

    std::printf("\nUse Home to close this loader screen.\n");
    while (appletMainLoop()) consoleUpdate(nullptr);
    consoleExit(nullptr);
    return 0;
}
