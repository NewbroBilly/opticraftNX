#pragma once
#include "java_runtime_bootstrap.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace opti::runtime {
struct ClasspathArchive {
    std::string path;
    const std::uint8_t* bytes=nullptr;
    std::size_t size=0;
    const JarInspection* inspection=nullptr;
};
struct ResolvedClass {
    bool found=false;
    std::size_t archive_index=0;
    std::string internal_name;
    JarEntry entry;
    std::string error;
};
// Resolves a binary Java class name against archives in classpath order.
// The returned JarEntry is copied; bytes remain owned by the caller.
ResolvedClass resolve_class(const std::string& binary_name,
                            const std::vector<ClasspathArchive>& archives);
bool read_resolved_class(const ResolvedClass& resolved,
                         const std::vector<ClasspathArchive>& archives,
                         std::vector<std::uint8_t>& class_bytes,
                         std::string* error=nullptr);
}
