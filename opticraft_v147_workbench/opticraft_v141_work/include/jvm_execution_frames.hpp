#pragma once
#include "jvm_runtime.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace opti::runtime {

// Diagnostic/checkpoint representation of one interpreter activation.
// It captures the state needed by a future resumable interpreter, but does
// not itself make the current recursive interpreter suspendable.
struct JvmExecutionFrame {
    std::string class_name;
    std::string method_name;
    std::string descriptor;
    std::size_t pc = 0;
    std::vector<JvmValue> locals;
    std::vector<JvmValue> operand_stack;
    ObjectRef receiver = 0;
    ObjectRef pending_exception = 0;
    std::uint64_t instruction_epoch = 0;
};

class JvmExecutionFrameStack {
public:
    void push(JvmExecutionFrame frame) { frames_.push_back(std::move(frame)); }
    void pop() {
        if (!frames_.empty()) {
            completed_.push_back(std::move(frames_.back()));
            frames_.pop_back();
        }
    }
    void update(std::size_t pc, const std::vector<JvmValue>& locals,
                const std::vector<JvmValue>& stack, ObjectRef exception) {
        if (frames_.empty()) return;
        auto& frame = frames_.back();
        frame.pc = pc;
        frame.locals = locals;
        frame.operand_stack = stack;
        frame.pending_exception = exception;
        ++frame.instruction_epoch;
    }
    const std::vector<JvmExecutionFrame>& frames() const { return frames_; }
    std::vector<JvmExecutionFrame> snapshot() const { return frames_; }
    // Replace the active call stack with a previously captured checkpoint.
    // Completed-frame history is deliberately preserved across restoration.
    bool restore(const std::vector<JvmExecutionFrame>& checkpoint) {
        if (checkpoint.size() > 256) return false;
        frames_ = checkpoint;
        return true;
    }
    std::vector<JvmExecutionFrame> detach() {
        auto saved = std::move(frames_);
        frames_.clear();
        return saved;
    }
    const std::vector<JvmExecutionFrame>& completed_frames() const { return completed_; }
    std::size_t depth() const { return frames_.size(); }
    void clear() { frames_.clear(); completed_.clear(); }
private:
    std::vector<JvmExecutionFrame> frames_;
    std::vector<JvmExecutionFrame> completed_;
};

} // namespace opti::runtime
