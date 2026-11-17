#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace opti::runtime {
struct BytecodeInstruction { std::uint32_t offset=0; std::uint8_t opcode=0; std::uint32_t length=0; };
struct BytecodeVerification {
 bool valid=false; std::string error;
 std::vector<BytecodeInstruction> instructions;
 std::vector<std::uint32_t> branch_targets;
};
BytecodeVerification verify_bytecode_structure(const std::uint8_t* code,std::size_t size);
}
