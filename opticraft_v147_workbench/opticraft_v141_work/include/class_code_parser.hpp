#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace opti::runtime {
struct ConstantPoolEntry {
    std::uint8_t tag=0;
    std::uint16_t a=0,b=0;
    std::int32_t integer=0;
    std::int64_t long_integer=0;
    std::string utf8;
};
struct ExceptionHandler {
    std::uint16_t start_pc=0,end_pc=0,handler_pc=0,catch_type=0;
};
struct FieldInfo {
    std::uint16_t access_flags=0;
    std::string name,descriptor;
    std::uint16_t constant_value_index=0;
};
struct MethodCode {
    std::uint16_t access_flags=0;
    std::string name,descriptor;
    std::uint16_t max_stack=0,max_locals=0;
    std::vector<std::uint8_t> code;
    std::vector<ExceptionHandler> exception_table;
};
struct ParsedClassCode {
    bool valid=false;
    std::string error;
    std::uint16_t minor=0,major=0,access_flags=0;
    std::string this_class,super_class;
    std::vector<std::string> interfaces;
    std::vector<ConstantPoolEntry> constant_pool;
    std::vector<FieldInfo> fields;
    std::vector<MethodCode> methods;
};
// Parses a complete classfile, extracting method Code attributes and exception tables.
ParsedClassCode parse_class_code(const std::uint8_t* bytes,std::size_t size);
}
