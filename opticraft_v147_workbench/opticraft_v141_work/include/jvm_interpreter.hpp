#pragma once
#include "class_code_parser.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace opti::runtime {
struct ExecutionResult { bool success=false; std::int32_t int_value=0; std::string error; std::size_t instructions_executed=0; };
class IntegerBytecodeInterpreter {
public:
 explicit IntegerBytecodeInterpreter(std::size_t instruction_budget=1000000):budget_(instruction_budget){}
 ExecutionResult invoke_static_int(const ParsedClassCode& cls,const std::string& method,
                                   const std::string& descriptor,const std::vector<std::int32_t>& args) const;
private: std::size_t budget_;
};
}
