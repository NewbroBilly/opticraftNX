#pragma once
#include "classpath_resolver.hpp"
#include <string>
#include <vector>

namespace opti::runtime {
enum class ArchiveRole { Bootstrap, Platform, Application };

struct RuntimeArchive {
    ArchiveRole role=ArchiveRole::Application;
    ClasspathArchive archive;
};

struct RuntimeClassPath {
    std::vector<ClasspathArchive> bootstrap;
    std::vector<ClasspathArchive> platform;
    std::vector<ClasspathArchive> application;
};

// Builds the deterministic parent-first search order:
// bootstrap -> platform -> application. Each role preserves insertion order.
std::vector<ClasspathArchive> parent_first_order(const RuntimeClassPath& path);

// Splits an explicit classpath string using the caller-supplied separator.
// Empty components are rejected rather than interpreted as the current directory.
bool split_classpath(const std::string& value, char separator,
                     std::vector<std::string>& paths,
                     std::string* error=nullptr);

struct LoadedClassBytes {
    bool found=false;
    ArchiveRole source_role=ArchiveRole::Application;
    std::size_t role_index=0;
    ResolvedClass resolved;
    std::vector<std::uint8_t> bytes;
    std::string error;
};

// Resolves and extracts using parent-first delegation. This is classpath
// infrastructure only; it does not define, verify, link, initialize or execute Java classes.
LoadedClassBytes load_class_bytes(const std::string& binary_name,
                                  const RuntimeClassPath& path);
}
