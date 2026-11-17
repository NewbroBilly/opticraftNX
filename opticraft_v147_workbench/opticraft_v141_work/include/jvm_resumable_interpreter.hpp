#pragma once
#include "class_code_parser.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace opti::runtime {

class ResumableIntegerInterpreter {
public:
    enum class Status { Ready, Running, Yielded, Completed, Failed };
    struct FrameView {
        std::string class_name, method_name, descriptor;
        std::size_t pc = 0;
        std::vector<std::int32_t> locals, operand_stack;
    };
    struct FrameCheckpoint {
        std::string class_name, method_name, descriptor;
        std::size_t pc = 0;
        std::vector<std::int32_t> locals, operand_stack;
    };
    struct Checkpoint { std::vector<FrameCheckpoint> frames; std::size_t total_instructions = 0; };
    struct Result {
        Status status = Status::Ready;
        std::int32_t value = 0;
        std::size_t instructions_executed = 0;
        std::string error;
    };

    bool begin(const ParsedClassCode& cls, const std::string& method,
               const std::string& descriptor, const std::vector<std::int32_t>& args);
    Result step(std::size_t instruction_quantum);
    Result run_to_completion(std::size_t quantum = 1024);
    std::vector<FrameView> frames() const;
    Checkpoint checkpoint() const;
    bool restore(const ParsedClassCode& cls, const Checkpoint& saved);
    bool restore_interpreter_checkpoint(const Checkpoint& saved);
    const Result& result() const { return result_; }
    void reset();

private:
    struct Frame {
        const ParsedClassCode* cls = nullptr;
        const MethodCode* method = nullptr;
        std::size_t pc = 0;
        std::vector<std::int32_t> locals, stack;
    };
    const ParsedClassCode* class_ = nullptr;
    std::vector<Frame> frames_;
    Result result_;
    std::size_t total_instructions_ = 0;
};

} // namespace opti::runtime
