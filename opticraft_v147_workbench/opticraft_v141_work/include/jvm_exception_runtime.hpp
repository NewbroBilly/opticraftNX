#pragma once
#include "class_code_parser.hpp"
#include "jvm_class_runtime.hpp"
#include <cstdint>
namespace opti::runtime {
struct ExceptionHandlerMatch { bool found=false; std::uint16_t handler_pc=0; std::uint16_t catch_type=0; };
ExceptionHandlerMatch find_exception_handler(const ParsedClassCode& cls,const MethodCode& method,std::uint16_t throw_pc,const std::string& thrown_class,const JvmClassRuntime& classes);
}
