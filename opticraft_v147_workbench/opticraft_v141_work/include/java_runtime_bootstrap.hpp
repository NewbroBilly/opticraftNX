#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace opti::runtime {
struct JarEntry {
    std::string name;
    std::uint32_t crc32=0, compressed_size=0, uncompressed_size=0, local_header_offset=0;
    std::uint16_t compression=0, flags=0;
};
struct JarInspection {
    bool valid=false;
    bool has_minecraft_main=false;
    std::size_t class_count=0, resource_count=0;
    std::vector<JarEntry> entries;
    std::string error;
};
JarInspection inspect_jar(const std::uint8_t* bytes,std::size_t size,
                           bool require_minecraft_main=true);
// Extracts one previously inspected entry, inflating raw DEFLATE when needed.
// Returns false for malformed ranges, unsupported methods, size mismatches or CRC errors.
bool extract_jar_entry(const std::uint8_t* archive,std::size_t archive_size,
                       const JarEntry& entry,std::vector<std::uint8_t>& output,
                       std::string* error=nullptr);
const JarEntry* find_jar_entry(const JarInspection& inspection,const std::string& name);

struct LaunchPlan {
    std::string data_root;
    std::string main_class="net.minecraft.client.Minecraft";
    std::vector<std::string> classpath;
    std::vector<std::string> arguments;
    std::string joined_classpath;
    bool valid=false;
    std::string error;
};
LaunchPlan make_launch_plan(const std::string& data_root,
                            const std::vector<std::string>& game_arguments={});

struct RuntimeCallbacks {
    void* user=nullptr;
    bool (*file_exists)(void*,const char*)=nullptr;
    bool (*load_vm)(void*,const char*,const char*)=nullptr;
    bool (*invoke_main)(void*,const char*,int,const char* const*)=nullptr;
};
struct BootstrapResult {
    bool success=false;
    std::string failed_step;
    std::string message;
};
// Orchestrates the launch contract. It does not implement or embed a JVM.
BootstrapResult bootstrap(const LaunchPlan&,const RuntimeCallbacks&);
}
