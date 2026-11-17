#include "java_thread_execution_runtime.hpp"
#include "class_code_parser.hpp"
#include <fstream>
#include <iterator>
#include <vector>
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace opti::runtime;

int main(int argc,char** argv) {
    JavaThreadExecutionRuntime runtime(8);
    int a_steps = 0;
    int b_steps = 0;
    const auto a = runtime.create("render", [&](JavaThreadExecutionRuntime::Context& ctx) {
        ++a_steps;
        JvmExecutionFrame frame; frame.class_name="game/Render"; frame.method_name="tick"; frame.pc=static_cast<std::size_t>(a_steps);
        if (ctx.frames.depth()==0) ctx.frames.push(frame); else ctx.frames.update(frame.pc, {}, {}, 0);
        return a_steps < 3;
    });
    const auto b = runtime.create("audio", [&](JavaThreadExecutionRuntime::Context& ctx) {
        ++b_steps;
        JvmExecutionFrame frame; frame.class_name="game/Audio"; frame.method_name="mix"; frame.pc=static_cast<std::size_t>(b_steps);
        if (ctx.frames.depth()==0) ctx.frames.push(frame); else ctx.frames.update(frame.pc, {}, {}, 0);
        return b_steps < 2;
    });
    assert(a && b && a != b);
    assert(runtime.start(a) && runtime.start(b));
    assert(runtime.run_ready(2)==2);
    assert(runtime.context(a)->frames.frames().front().pc==1);
    JavaThreadExecutionRuntime::Checkpoint saved;
    assert(runtime.checkpoint(a, saved));
    assert(saved.frames.size()==1 && saved.frames.front().pc==1);
    assert(runtime.context(b)->frames.frames().front().pc==1);
    assert(runtime.park(a));
    assert(runtime.state(a)==JavaThreadScheduler::State::Parked);
    assert(runtime.is_alive(a));
    assert(runtime.run_ready(4)==1);
    assert(a_steps==1 && b_steps==2);
    assert(runtime.state(b)==JavaThreadScheduler::State::Terminated);
    assert(runtime.resume(a));
    runtime.context(a)->frames.update(777, {std::int32_t(9)}, {std::int32_t(11)}, 0);
    assert(runtime.restore(saved));
    assert(runtime.context(a)->frames.frames().front().pc==1);
    assert(runtime.context(a)->scheduler_steps==saved.scheduler_steps);
    assert(runtime.run_ready(4)==2);
    assert(a_steps==3);
    assert(runtime.state(a)==JavaThreadScheduler::State::Terminated);
    assert(!runtime.checkpoint(9999, saved));

    JvmExecutionFrameStack stack;
    JvmExecutionFrame f; f.class_name="game/Main"; f.method_name="main"; f.pc=42;
    stack.push(f);
    const auto checkpoint=stack.snapshot();
    stack.update(99, {}, {std::int32_t(5)}, 0);
    assert(stack.restore(checkpoint));
    assert(stack.frames().back().pc==42);
    auto detached=stack.detach();
    assert(detached.size()==1 && stack.depth()==0);
    assert(stack.restore(detached));
    assert(stack.depth()==1);
    // The scheduler now drives real resumable bytecode slices, not only native callbacks.
    assert(argc == 2);
    std::ifstream class_file(argv[1],std::ios::binary); assert(class_file.good());
    std::vector<std::uint8_t> class_bytes((std::istreambuf_iterator<char>(class_file)),{});
    auto parsed=parse_class_code(class_bytes.data(),class_bytes.size()); assert(parsed.valid);
    JavaThreadExecutionRuntime bytecode_runtime(4);
    auto sum_id=bytecode_runtime.create_interpreted_integer("java-sum",parsed,"sum","(I)I",{100},5);
    auto fact_id=bytecode_runtime.create_interpreted_integer("java-factorial",parsed,"factorial","(I)I",{6},3);
    assert(sum_id&&fact_id&&bytecode_runtime.start(sum_id)&&bytecode_runtime.start(fact_id));
    assert(bytecode_runtime.run_ready(2)==2);
    JavaThreadExecutionRuntime::Checkpoint bytecode_checkpoint;
    assert(bytecode_runtime.checkpoint(sum_id,bytecode_checkpoint));
    assert(bytecode_checkpoint.has_interpreter&&!bytecode_checkpoint.interpreter.frames.empty());
    assert(bytecode_runtime.context(sum_id)->frames.depth()>0);
    assert(bytecode_runtime.park(sum_id));
    while(bytecode_runtime.is_alive(fact_id)) bytecode_runtime.run_ready(8);
    assert(bytecode_runtime.context(fact_id)->integer_result==720);
    // Advance the parked thread after resuming, then restore its true interpreter PC/locals/stack.
    assert(bytecode_runtime.resume(sum_id));
    assert(bytecode_runtime.run_ready(1)==1);
    assert(bytecode_runtime.park(sum_id));
    assert(bytecode_runtime.restore(bytecode_checkpoint));
    assert(bytecode_runtime.context(sum_id)->frames.frames().front().pc==bytecode_checkpoint.frames.front().pc);
    assert(bytecode_runtime.resume(sum_id));
    while(bytecode_runtime.is_alive(sum_id)) bytecode_runtime.run_ready(8);
    assert(bytecode_runtime.context(sum_id)->integer_result==5050);
    assert(bytecode_runtime.state(sum_id)==JavaThreadScheduler::State::Terminated);

    std::cout << "Java thread execution contexts and frame checkpoints: PASS\n";
}
