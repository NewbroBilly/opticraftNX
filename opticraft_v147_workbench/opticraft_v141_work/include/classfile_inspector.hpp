#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace opti::runtime {
struct ClassInspection {
    bool valid=false;
    std::string error;
    std::uint16_t minor=0, major=0;
    std::uint16_t constant_pool_count=0;
    std::uint16_t fields_count=0, methods_count=0, attributes_count=0;
    std::string this_class, super_class;
    // Symbolic class references found in CONSTANT_Class entries and descriptors.
    // This is dependency-discovery metadata, not bytecode verification.
    std::vector<std::string> referenced_classes;
    std::size_t byte_size=0;
};
// Validates complete classfile structure and constant-pool reference types.
// Does not verify Java bytecode or execute a class.
ClassInspection inspect_classfile(const std::uint8_t* data,std::size_t size);
}
