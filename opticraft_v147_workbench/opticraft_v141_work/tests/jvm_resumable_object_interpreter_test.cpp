#include "class_code_parser.hpp"
#include "jvm_resumable_object_interpreter.hpp"
#include "java_thread_execution_runtime.hpp"
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace opti::runtime;
int main(int argc,char**argv) {
    assert(argc==2);
    std::ifstream in(argv[1],std::ios::binary); assert(in.good());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
    auto parsed=parse_class_code(bytes.data(),bytes.size()); assert(parsed.valid);
    JvmHeap heap; JvmStaticFields statics; JvmClassRuntime classes;
    auto registered=classes.register_class(std::move(parsed)); assert(registered.success);
    JvmResumableObjectInterpreter vm(heap,statics,classes);
    assert(vm.begin_static("ResumeObjectFixture","twice","(I)I",{std::int32_t(21)}));
    auto first=vm.step(3); assert(first.status==JvmResumableObjectInterpreter::Status::Yielded);
    auto frames=vm.frames(); assert(frames.size()==2 && frames.front().pc>0);
    auto checkpoint=vm.checkpoint();
    JvmResumableObjectInterpreter restored_vm(heap,statics,classes);
    std::string restore_error; assert(restored_vm.restore(checkpoint,restore_error));
    auto done=restored_vm.run_to_completion(2);
    assert(done.status==JvmResumableObjectInterpreter::Status::Completed);
    assert(std::get<std::int32_t>(done.value)==42);

    std::vector<FieldLayout> layout; std::string error;
    assert(classes.build_instance_layout("ResumeObjectFixture",layout,error));
    auto object=heap.allocate_instance("ResumeObjectFixture",layout); assert(object);
    assert(heap.put_field(object,"ResumeObjectFixture.value",std::int32_t(37)));
    assert(vm.begin_instance(object,"add","(I)I",{std::int32_t(5)}));
    auto slice=vm.step(1); assert(slice.status==JvmResumableObjectInterpreter::Status::Yielded);
    auto instance_done=vm.run_to_completion(3);
    assert(instance_done.status==JvmResumableObjectInterpreter::Status::Completed);
    assert(std::get<std::int32_t>(instance_done.value)==42);

    assert(vm.begin_static("ResumeObjectFixture","arrayWork","()I",{}));
    auto array_done=vm.run_to_completion(2);
    assert(array_done.status==JvmResumableObjectInterpreter::Status::Completed);
    assert(std::get<std::int32_t>(array_done.value)==42);
    assert(vm.begin_static("ResumeObjectFixture","staticWork","()I",{}));
    auto static_done=vm.run_to_completion(2);
    assert(static_done.status==JvmResumableObjectInterpreter::Status::Completed);
    assert(std::get<std::int32_t>(static_done.value)==42);
    assert(vm.begin_static("ResumeObjectFixture","classInitWork","()I",{}));
    auto class_init=vm.run_to_completion(1);
    assert(class_init.status==JvmResumableObjectInterpreter::Status::Completed);
    assert(std::get<std::int32_t>(class_init.value)==147);
    const auto exception_ref=heap.allocate_object("ResumeObjectFixture$MyException"); assert(exception_ref);
    assert(vm.begin_static("ResumeObjectFixture","catchWork","(Ljava/lang/Object;)I",{exception_ref}));
    auto caught=vm.run_to_completion(2);
    assert(caught.status==JvmResumableObjectInterpreter::Status::Completed);
    assert(std::get<std::int32_t>(caught.value)==42);

    // Runtime exceptions must be real heap objects and must unwind through
    // nested Java calls using the caller's invoke bytecode PC.
    assert(vm.begin_static("ResumeObjectFixture","catchArithmetic","(I)I",{std::int32_t(0)}));
    auto arithmetic=vm.run_to_completion(1);
    assert(arithmetic.status==JvmResumableObjectInterpreter::Status::Completed);
    assert(std::get<std::int32_t>(arithmetic.value)==53);

    assert(vm.begin_static("ResumeObjectFixture","catchNull","([Ljava/lang/Object;)I",{NullValue{}}));
    auto null_result=vm.run_to_completion(1);
    assert(null_result.status==JvmResumableObjectInterpreter::Status::Completed);
    assert(std::get<std::int32_t>(null_result.value)==54);

    const auto strings=heap.allocate_reference_array(1,"java/lang/String");
    const auto plain_object=heap.allocate_object("java/lang/Object");
    assert(strings&&plain_object);
    assert(vm.begin_static("ResumeObjectFixture","catchStore","([Ljava/lang/Object;Ljava/lang/Object;)I",{strings,plain_object}));
    auto store_result=vm.run_to_completion(1);
    assert(store_result.status==JvmResumableObjectInterpreter::Status::Completed);
    assert(std::get<std::int32_t>(store_result.value)==55);

    const auto ints=heap.allocate_int_array(1); assert(ints);
    assert(vm.begin_static("ResumeObjectFixture","catchBounds","([I)I",{ints}));
    auto bounds_result=vm.run_to_completion(1);
    assert(bounds_result.status==JvmResumableObjectInterpreter::Status::Completed);
    assert(std::get<std::int32_t>(bounds_result.value)==56);

    const auto wrong_type=heap.allocate_object("java/lang/Object"); assert(wrong_type);
    assert(vm.begin_static("ResumeObjectFixture","catchCast","(Ljava/lang/Object;)I",{wrong_type}));
    auto cast_result=vm.run_to_completion(1);
    assert(cast_result.status==JvmResumableObjectInterpreter::Status::Completed);
    assert(std::get<std::int32_t>(cast_result.value)==57);

    // The same typed object interpreter can now run as a cooperative Java thread.
    JavaThreadExecutionRuntime threads(4);
    auto scheduled=threads.create_interpreted_object("object-worker",heap,statics,classes,
        "ResumeObjectFixture",0,"twice","(I)I",{std::int32_t(33)},1);
    assert(scheduled && threads.start(scheduled));
    assert(threads.run_ready(1)==1);
    JavaThreadExecutionRuntime::Checkpoint thread_checkpoint;
    assert(threads.checkpoint(scheduled,thread_checkpoint));
    assert(thread_checkpoint.has_object_interpreter);
    assert(threads.run_ready(1)==1);
    assert(threads.restore(thread_checkpoint));
    while (threads.is_alive(scheduled)) threads.run_ready(1);
    assert(threads.state(scheduled)==JavaThreadScheduler::State::Terminated);
    assert(std::get<std::int32_t>(threads.context(scheduled)->object_result)==66);
    assert(threads.context(scheduled)->scheduler_steps>1);
    std::cout << "resumable object interpreter PASS; checkpoint/restore, runtime exception propagation, aastore type safety, arrays, static fields, instance calls, scheduler\n";
}
