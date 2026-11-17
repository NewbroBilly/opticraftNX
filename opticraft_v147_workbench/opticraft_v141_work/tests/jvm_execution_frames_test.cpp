#include "jvm_execution_frames.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace opti::runtime;
int main() {
    JvmExecutionFrameStack frames;
    JvmExecutionFrame outer;
    outer.class_name = "game/Main";
    outer.method_name = "main";
    outer.descriptor = "([Ljava/lang/String;)V";
    outer.locals = {std::int32_t(7)};
    frames.push(outer);
    frames.update(12, {std::int32_t(9)}, {std::int32_t(3)}, 0);
    assert(frames.depth() == 1);
    assert(frames.frames().back().pc == 12);
    assert(std::get<std::int32_t>(frames.frames().back().locals[0]) == 9);
    assert(std::get<std::int32_t>(frames.frames().back().operand_stack[0]) == 3);

    JvmExecutionFrame inner;
    inner.class_name = "game/World";
    inner.method_name = "tick";
    inner.descriptor = "()V";
    inner.receiver = 42;
    frames.push(inner);
    frames.update(5, {}, {std::int32_t(1)}, 77);
    const auto suspended = frames.snapshot();
    assert(suspended.size() == 2);
    assert(suspended[1].pc == 5 && suspended[1].pending_exception == 77);
    assert(suspended[1].receiver == 42);
    frames.pop();
    assert(frames.depth() == 1);
    assert(frames.completed_frames().size() == 1);
    assert(frames.completed_frames()[0].class_name == "game/World");
    frames.pop();
    assert(frames.depth() == 0);
    assert(frames.completed_frames().size() == 2);
    frames.clear();
    assert(frames.completed_frames().empty());
    std::cout << "JVM execution frame snapshots: PASS\n";
}
