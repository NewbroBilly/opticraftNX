#pragma once
#include "jvm_class_runtime.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace opti::runtime {

// Cooperative, typed/reference-aware interpreter used as the next migration
// layer for Minecraft's object interpreter. Each step executes a bounded
// number of bytecodes and preserves the complete active call chain.
class JvmResumableObjectInterpreter {
public:
    enum class Status { Ready, Yielded, Completed, Failed };
    struct FrameView {
        std::string class_name, method_name, descriptor;
        std::size_t pc = 0;
        ObjectRef receiver = 0;
        std::size_t call_site_pc = 0;
        std::string initializer_class;
        std::vector<JvmValue> locals, operand_stack;
    };
    struct FrameSnapshot {
        std::string class_name, method_name, descriptor;
        std::size_t pc = 0;
        ObjectRef receiver = 0;
        std::size_t call_site_pc = 0;
        std::string initializer_class;
        std::vector<JvmValue> locals, operand_stack;
    };
    struct Snapshot {
        std::vector<FrameSnapshot> frames;
        Status status = Status::Ready;
        JvmValue value = NullValue{};
        std::size_t total_instructions = 0;
        std::string error;
        std::vector<std::string> initialized_classes;
        std::vector<std::string> initializing_classes;
    };
    struct Result {
        Status status = Status::Ready;
        JvmValue value = NullValue{};
        std::size_t instructions_executed = 0;
        std::string error;
    };

    JvmResumableObjectInterpreter(JvmHeap& heap, JvmStaticFields& statics,
                                  JvmClassRuntime& classes,
                                  std::size_t max_call_depth = 256);
    bool begin_static(const std::string& class_name, const std::string& method,
                      const std::string& descriptor, const std::vector<JvmValue>& args);
    bool begin_instance(ObjectRef receiver, const std::string& method,
                        const std::string& descriptor, const std::vector<JvmValue>& args);
    Result step(std::size_t quantum);
    Result run_to_completion(std::size_t quantum = 256);
    std::vector<FrameView> frames() const;
    Snapshot checkpoint() const;
    bool restore(const Snapshot& snapshot, std::string& error);
    const Result& result() const { return result_; }
    void reset();

private:
    struct Frame {
        const ParsedClassCode* cls = nullptr;
        const MethodCode* method = nullptr;
        std::size_t pc = 0;
        ObjectRef receiver = 0;
        std::size_t call_site_pc = 0;
        std::string initializer_class;
        std::vector<JvmValue> locals, stack;
    };
    JvmHeap& heap_;
    JvmStaticFields& statics_;
    JvmClassRuntime& classes_;
    std::size_t max_call_depth_;
    std::vector<Frame> frames_;
    Result result_;
    std::size_t total_instructions_ = 0;
    std::vector<std::string> initialized_classes_;
    std::vector<std::string> initializing_classes_;
    bool push_frame(const ResolvedMethod&, ObjectRef, const std::vector<JvmValue>&);
    Result fail(const std::string& message);
    bool dispatch_exception(Frame& frame, std::size_t throw_pc, ObjectRef exception);
    bool propagate_exception(std::size_t throw_pc, ObjectRef exception);
    bool raise_exception(std::size_t throw_pc, const std::string& class_name, const std::string& message);
    bool initialize_static_storage(const ParsedClassCode& cls);
};

} // namespace opti::runtime
