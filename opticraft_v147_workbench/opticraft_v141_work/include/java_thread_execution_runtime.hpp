#pragma once
#include "java_thread_scheduler.hpp"
#include "jvm_execution_frames.hpp"
#include "jvm_resumable_interpreter.hpp"
#include "jvm_resumable_object_interpreter.hpp"
#include <stdexcept>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace opti::runtime {

// Owns one independent interpreter checkpoint per scheduled Java thread.
// The step callback is cooperative: it must execute a bounded amount of work
// and return true to yield, or false when the Java thread has completed.
class JavaThreadExecutionRuntime {
public:
    using ThreadId = JavaThreadScheduler::ThreadId;
    struct Context {
        ThreadId id = 0;
        std::string name;
        JvmExecutionFrameStack frames;
        std::uint64_t scheduler_steps = 0;
        std::unique_ptr<ResumableIntegerInterpreter> integer_interpreter;
        std::unique_ptr<JvmResumableObjectInterpreter> object_interpreter;
        JvmValue object_result = NullValue{};
        std::int32_t integer_result = 0;
        std::string execution_error;
    };
    using Step = std::function<bool(Context&)>;
    struct Checkpoint {
        ThreadId id = 0;
        std::vector<JvmExecutionFrame> frames;
        std::uint64_t scheduler_steps = 0;
        bool has_interpreter = false;
        ResumableIntegerInterpreter::Checkpoint interpreter;
        bool has_object_interpreter = false;
        JvmResumableObjectInterpreter::Snapshot object_interpreter;
    };

    explicit JavaThreadExecutionRuntime(std::size_t max_threads = 256)
        : scheduler_(max_threads) {}

    ThreadId create(std::string name, Step step, bool daemon = false) {
        if (!step) return 0;
        auto id_holder = std::make_shared<ThreadId>(0);
        const ThreadId id = scheduler_.create(name, [this, id_holder, step]() mutable {
            auto it = contexts_.find(*id_holder);
            if (it == contexts_.end()) return false;
            ++it->second.scheduler_steps;
            return step(it->second);
        }, daemon);
        if (!id) return 0;
        *id_holder = id;
        contexts_.emplace(id, Context{id, std::move(name), {}, 0, nullptr, nullptr, NullValue{}, 0, {}});
        return id;
    }

    // Runs a supported static int Java method as bounded interpreter slices on this scheduler.
    // The ParsedClassCode object must outlive the scheduled thread.
    ThreadId create_interpreted_integer(std::string name, const ParsedClassCode& cls,
        const std::string& method, const std::string& descriptor,
        const std::vector<std::int32_t>& args, std::size_t quantum, bool daemon = false) {
        if (quantum == 0) return 0;
        auto interpreter = std::make_unique<ResumableIntegerInterpreter>();
        if (!interpreter->begin(cls,method,descriptor,args)) return 0;
        auto id_holder = std::make_shared<ThreadId>(0);
        const ThreadId id = scheduler_.create(name, [this, id_holder, quantum]() {
            auto it = contexts_.find(*id_holder);
            if (it == contexts_.end() || !it->second.integer_interpreter) return false;
            auto& ctx = it->second;
            ++ctx.scheduler_steps;
            const auto result = ctx.integer_interpreter->step(quantum);
            ctx.frames.clear();
            for (const auto& source : ctx.integer_interpreter->frames()) {
                JvmExecutionFrame frame; frame.class_name=source.class_name; frame.method_name=source.method_name;
                frame.descriptor=source.descriptor; frame.pc=source.pc;
                for (auto value : source.locals) frame.locals.emplace_back(value);
                for (auto value : source.operand_stack) frame.operand_stack.emplace_back(value);
                ctx.frames.push(std::move(frame));
            }
            if (result.status == ResumableIntegerInterpreter::Status::Failed) {
                ctx.execution_error = result.error; throw std::runtime_error(result.error);
            }
            if (result.status == ResumableIntegerInterpreter::Status::Completed) {
                ctx.integer_result = result.value; return false;
            }
            return true;
        }, daemon);
        if (!id) return 0;
        *id_holder=id;
        Context ctx; ctx.id=id; ctx.name=std::move(name); ctx.integer_interpreter=std::move(interpreter);
        contexts_.emplace(id,std::move(ctx));
        return id;
    }

    // Schedules a typed/reference-aware object-interpreter activation. Heap,
    // statics and class runtime must outlive this scheduled thread.
    ThreadId create_interpreted_object(std::string name, JvmHeap& heap,
        JvmStaticFields& statics, JvmClassRuntime& classes,
        const std::string& owner_or_receiver_class, ObjectRef receiver,
        const std::string& method, const std::string& descriptor,
        const std::vector<JvmValue>& args, std::size_t quantum, bool daemon = false) {
        if (!quantum) return 0;
        auto interpreter = std::make_unique<JvmResumableObjectInterpreter>(heap, statics, classes);
        const bool begun = receiver
            ? interpreter->begin_instance(receiver, method, descriptor, args)
            : interpreter->begin_static(owner_or_receiver_class, method, descriptor, args);
        if (!begun) return 0;
        auto id_holder = std::make_shared<ThreadId>(0);
        const ThreadId id = scheduler_.create(name, [this, id_holder, quantum]() {
            auto it = contexts_.find(*id_holder);
            if (it == contexts_.end() || !it->second.object_interpreter) return false;
            auto& ctx = it->second;
            ++ctx.scheduler_steps;
            const auto result = ctx.object_interpreter->step(quantum);
            ctx.frames.clear();
            for (const auto& source : ctx.object_interpreter->frames()) {
                JvmExecutionFrame frame;
                frame.class_name = source.class_name; frame.method_name = source.method_name;
                frame.descriptor = source.descriptor; frame.pc = source.pc;
                frame.receiver = source.receiver; frame.locals = source.locals;
                frame.operand_stack = source.operand_stack;
                ctx.frames.push(std::move(frame));
            }
            if (result.status == JvmResumableObjectInterpreter::Status::Failed) {
                ctx.execution_error = result.error; throw std::runtime_error(result.error);
            }
            if (result.status == JvmResumableObjectInterpreter::Status::Completed) {
                ctx.object_result = result.value; return false;
            }
            return true;
        }, daemon);
        if (!id) return 0;
        *id_holder = id;
        Context ctx; ctx.id = id; ctx.name = std::move(name);
        ctx.object_interpreter = std::move(interpreter);
        contexts_.emplace(id, std::move(ctx));
        return id;
    }

    bool start(ThreadId id) { return scheduler_.start(id); }
    std::size_t run_ready(std::size_t max_steps) { return scheduler_.run_ready(max_steps); }
    bool park(ThreadId id) { return scheduler_.park(id); }
    bool resume(ThreadId id) { return scheduler_.resume(id); }
    bool interrupt(ThreadId id) { return scheduler_.interrupt(id); }
    bool interrupted(ThreadId id, bool clear = false) { return scheduler_.interrupted(id, clear); }
    bool is_alive(ThreadId id) const { return scheduler_.is_alive(id); }
    JavaThreadScheduler::State state(ThreadId id) const { return scheduler_.state(id); }
    Context* context(ThreadId id) { auto it=contexts_.find(id); return it==contexts_.end()?nullptr:&it->second; }
    const Context* context(ThreadId id) const { auto it=contexts_.find(id); return it==contexts_.end()?nullptr:&it->second; }
    std::vector<JavaThreadScheduler::Snapshot> snapshot() const { return scheduler_.snapshot(); }
    bool checkpoint(ThreadId id, Checkpoint& out) const {
        const auto* ctx = context(id);
        if (!ctx) return false;
        out.id = id;
        out.frames = ctx->frames.snapshot();
        out.scheduler_steps = ctx->scheduler_steps;
        out.has_interpreter = static_cast<bool>(ctx->integer_interpreter);
        if (ctx->integer_interpreter) out.interpreter = ctx->integer_interpreter->checkpoint();
        out.has_object_interpreter = static_cast<bool>(ctx->object_interpreter);
        if (ctx->object_interpreter) out.object_interpreter = ctx->object_interpreter->checkpoint();
        return true;
    }
    bool restore(const Checkpoint& saved) {
        auto* ctx = context(saved.id);
        if (!ctx || saved.frames.size() > 256) return false;
        if (!ctx->frames.restore(saved.frames)) return false;
        if (saved.has_interpreter) {
            if (!ctx->integer_interpreter) return false;
            if (!ctx->integer_interpreter->restore_interpreter_checkpoint(saved.interpreter)) return false;
        }
        if (saved.has_object_interpreter) {
            if (!ctx->object_interpreter) return false;
            std::string error;
            if (!ctx->object_interpreter->restore(saved.object_interpreter, error)) return false;
        }
        ctx->scheduler_steps = saved.scheduler_steps;
        return true;
    }

private:
    JavaThreadScheduler scheduler_;
    std::map<ThreadId, Context> contexts_;
};

} // namespace opti::runtime
