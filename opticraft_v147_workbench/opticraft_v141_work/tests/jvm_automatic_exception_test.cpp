#include "jvm_class_runtime.hpp"
#include "jvm_object_interpreter.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace opti::runtime;

namespace {
ConstantPoolEntry utf(const char* s) { ConstantPoolEntry e; e.tag = 1; e.utf8 = s; return e; }
std::uint16_t add_utf(ParsedClassCode& c, const char* s) {
    c.constant_pool.push_back(utf(s));
    return static_cast<std::uint16_t>(c.constant_pool.size() - 1);
}
std::uint16_t add_class(ParsedClassCode& c, const char* name) {
    const auto n = add_utf(c, name); ConstantPoolEntry e; e.tag = 7; e.a = n;
    c.constant_pool.push_back(e); return static_cast<std::uint16_t>(c.constant_pool.size() - 1);
}
std::uint16_t add_nat(ParsedClassCode& c, const char* name, const char* desc) {
    const auto n = add_utf(c, name), d = add_utf(c, desc);
    ConstantPoolEntry e; e.tag = 12; e.a = n; e.b = d;
    c.constant_pool.push_back(e); return static_cast<std::uint16_t>(c.constant_pool.size() - 1);
}
std::uint16_t add_member_ref(ParsedClassCode& c, std::uint8_t tag,
                             std::uint16_t owner, std::uint16_t nat) {
    ConstantPoolEntry e; e.tag = tag; e.a = owner; e.b = nat;
    c.constant_pool.push_back(e); return static_cast<std::uint16_t>(c.constant_pool.size() - 1);
}
MethodCode make_method(const char* name, std::uint16_t stack,
                       std::initializer_list<std::uint8_t> code,
                       std::uint16_t catch_type, std::uint16_t start,
                       std::uint16_t end, std::uint16_t handler) {
    MethodCode m; m.access_flags = 0x0008; m.name = name; m.descriptor = "()I";
    m.max_stack = stack; m.max_locals = 0; m.code.assign(code);
    m.exception_table.push_back({start, end, handler, catch_type});
    return m;
}
ParsedClassCode exception_class(const char* name, const char* parent) {
    ParsedClassCode c; c.valid = true; c.this_class = name; c.super_class = parent;
    c.constant_pool.resize(1); return c;
}
}

int main() {
    JvmClassRuntime classes;
    auto object = exception_class("java/lang/Object", "");
    auto throwable = exception_class("java/lang/Throwable", "java/lang/Object");
    auto exception = exception_class("java/lang/Exception", "java/lang/Throwable");
    auto runtime_exception = exception_class("java/lang/RuntimeException", "java/lang/Exception");
    auto arithmetic = exception_class("java/lang/ArithmeticException", "java/lang/Throwable");
    auto npe = exception_class("java/lang/NullPointerException", "java/lang/Throwable");
    auto bounds = exception_class("java/lang/ArrayIndexOutOfBoundsException", "java/lang/Throwable");
    auto negative_size = exception_class("java/lang/NegativeArraySizeException", "java/lang/Throwable");
    assert(classes.register_class(object).success);
    assert(classes.register_class(throwable).success);
    assert(classes.register_class(exception).success);
    assert(classes.register_class(runtime_exception).success);
    assert(classes.register_class(arithmetic).success);
    assert(classes.register_class(npe).success);
    assert(classes.register_class(bounds).success);
    assert(classes.register_class(negative_size).success);

    ParsedClassCode tests; tests.valid = true; tests.this_class = "demo/AutomaticExceptions";
    tests.super_class = "java/lang/Object"; tests.constant_pool.resize(1);
    const auto arithmetic_type = add_class(tests, "java/lang/ArithmeticException");
    const auto npe_type = add_class(tests, "java/lang/NullPointerException");
    const auto bounds_type = add_class(tests, "java/lang/ArrayIndexOutOfBoundsException");
    const auto negative_size_type = add_class(tests, "java/lang/NegativeArraySizeException");
    const auto owner_class = add_class(tests, "demo/Holder");
    const auto field_nat = add_nat(tests, "value", "I");
    const auto field_ref = add_member_ref(tests, 9, owner_class, field_nat);
    const auto target_class = add_class(tests, "demo/Target");
    const auto target_nat = add_nat(tests, "read", "()I");
    const auto target_ref = add_member_ref(tests, 10, target_class, target_nat);

    // idiv by zero -> typed catch -> return 7
    tests.methods.push_back(make_method("catchDivideByZero", 2,
        {0x04, 0x03, 0x6c, 0xac, 0x57, 0x10, 7, 0xac},
        arithmetic_type, 2, 3, 4));
    tests.methods.push_back(make_method("catchRemainderByZero", 2,
        {0x04, 0x03, 0x70, 0xac, 0x57, 0x10, 16, 0xac},
        arithmetic_type, 2, 3, 4));
    // arraylength(null) -> typed catch -> return 8
    tests.methods.push_back(make_method("catchNullArrayLength", 1,
        {0x01, 0xbe, 0xac, 0x57, 0x10, 8, 0xac},
        npe_type, 1, 2, 3));
    // int[1][2] -> iaload bounds fault -> typed catch -> return 9
    tests.methods.push_back(make_method("catchArrayBounds", 2,
        {0x04, 0xbc, 10, 0x05, 0x2e, 0xac, 0x57, 0x10, 9, 0xac},
        bounds_type, 4, 5, 6));
    tests.methods.push_back(make_method("catchArrayStoreBounds", 3,
        {0x04, 0xbc, 10, 0x05, 0x04, 0x4f, 0x03, 0xac, 0x57, 0x10, 15, 0xac},
        bounds_type, 5, 6, 8));
    // Negative array size -> NegativeArraySizeException.
    tests.methods.push_back(make_method("catchNegativeArraySize", 1,
        {0x02, 0xbc, 10, 0x57, 0x10, 10, 0xac},
        negative_size_type, 1, 3, 3));
    // getfield(null) and putfield(null) both become catchable NPEs.
    tests.methods.push_back(make_method("catchNullGetField", 1,
        {0x01, 0xb4, std::uint8_t(field_ref >> 8), std::uint8_t(field_ref), 0xac, 0x57, 0x10, 11, 0xac},
        npe_type, 1, 4, 5));
    tests.methods.push_back(make_method("catchNullPutField", 2,
        {0x01, 0x04, 0xb5, std::uint8_t(field_ref >> 8), std::uint8_t(field_ref), 0x03, 0xac, 0x57, 0x10, 12, 0xac},
        npe_type, 2, 5, 7));
    // invokevirtual(null) is rejected before method resolution and is catchable.
    tests.methods.push_back(make_method("catchNullInvoke", 1,
        {0x01, 0xb6, std::uint8_t(target_ref >> 8), std::uint8_t(target_ref), 0xac, 0x57, 0x10, 13, 0xac},
        npe_type, 1, 4, 5));

    assert(classes.register_class(tests).success);

    // A fault in a callee must unwind and be caught by the caller's typed handler.
    ParsedClassCode worker; worker.valid = true; worker.this_class = "demo/ArithmeticWorker";
    worker.super_class = "java/lang/Object"; worker.constant_pool.resize(1);
    MethodCode worker_method; worker_method.access_flags = 0x0008;
    worker_method.name = "explode"; worker_method.descriptor = "()I";
    worker_method.max_stack = 2; worker_method.max_locals = 0;
    worker_method.code = {0x04, 0x03, 0x6c, 0xac};
    worker.methods.push_back(worker_method);
    ParsedClassCode boundary; boundary.valid = true; boundary.this_class = "demo/ArithmeticBoundary";
    boundary.super_class = "java/lang/Object"; boundary.constant_pool.resize(1);
    const auto worker_class = add_class(boundary, "demo/ArithmeticWorker");
    const auto worker_nat = add_nat(boundary, "explode", "()I");
    const auto worker_ref = add_member_ref(boundary, 10, worker_class, worker_nat);
    const auto boundary_arithmetic_type = add_class(boundary, "java/lang/ArithmeticException");
    auto boundary_method = make_method("catchAcrossFrame", 1,
        {0xb8, std::uint8_t(worker_ref >> 8), std::uint8_t(worker_ref), 0xac, 0x57, 0x10, 14, 0xac},
        boundary_arithmetic_type, 0, 3, 4);
    boundary.methods.push_back(boundary_method);
    assert(classes.register_class(worker).success);
    assert(classes.register_class(boundary).success);


    // Throwable constructor/getMessage/printStackTrace diagnostics are retained
    // instead of being silently discarded by the host probe.
    ParsedClassCode diagnostic; diagnostic.valid = true;
    diagnostic.this_class = "demo/ThrowableDiagnostics";
    diagnostic.super_class = "java/lang/Object";
    diagnostic.constant_pool.resize(1);
    const auto runtime_type = add_class(diagnostic, "java/lang/RuntimeException");
    const auto throwable_type = add_class(diagnostic, "java/lang/Throwable");
    const auto message_utf = add_utf(diagnostic, "menu bootstrap failed");
    ConstantPoolEntry message_constant; message_constant.tag = 8; message_constant.a = message_utf;
    diagnostic.constant_pool.push_back(message_constant);
    const auto init_nat = add_nat(diagnostic, "<init>", "(Ljava/lang/String;)V");
    const auto init_ref = add_member_ref(diagnostic, 10, runtime_type, init_nat);
    const auto print_nat = add_nat(diagnostic, "printStackTrace", "()V");
    const auto print_ref = add_member_ref(diagnostic, 10, throwable_type, print_nat);
    MethodCode diagnostic_method; diagnostic_method.access_flags = 0x0008;
    diagnostic_method.name = "run"; diagnostic_method.descriptor = "()I";
    diagnostic_method.max_stack = 3; diagnostic_method.max_locals = 0;
    diagnostic_method.code = {
        0xbb, std::uint8_t(runtime_type >> 8), std::uint8_t(runtime_type),
        0x59, 0x12, std::uint8_t(6),
        0xb7, std::uint8_t(init_ref >> 8), std::uint8_t(init_ref),
        0x59, 0xb6, std::uint8_t(print_ref >> 8), std::uint8_t(print_ref),
        0x57, 0x04, 0xac
    };
    diagnostic.methods.push_back(diagnostic_method);
    assert(classes.register_class(diagnostic).success);

    JvmHeap heap; JvmStaticFields statics;
    ObjectBytecodeInterpreter vm(heap, statics, 10000, &classes);
    const auto div = vm.invoke_static(tests, "catchDivideByZero", "()I", {});
    if (!div.success) std::cerr << "divide exception: " << div.error << '\n';
    assert(div.success && std::get<std::int32_t>(div.value) == 7);
    assert(vm.diagnostics().find("THROW java/lang/ArithmeticException") != std::string::npos);
    assert(vm.diagnostics().find("catchDivideByZero") != std::string::npos);
    const auto rem = vm.invoke_static(tests, "catchRemainderByZero", "()I", {});
    if (!rem.success) std::cerr << "remainder exception: " << rem.error << '\n';
    assert(rem.success && std::get<std::int32_t>(rem.value) == 16);
    const auto null_length = vm.invoke_static(tests, "catchNullArrayLength", "()I", {});
    if (!null_length.success) std::cerr << "null exception: " << null_length.error << '\n';
    assert(null_length.success && std::get<std::int32_t>(null_length.value) == 8);
    const auto array_bounds = vm.invoke_static(tests, "catchArrayBounds", "()I", {});
    if (!array_bounds.success) std::cerr << "bounds exception: " << array_bounds.error << '\n';
    assert(array_bounds.success && std::get<std::int32_t>(array_bounds.value) == 9);
    const auto array_store_bounds = vm.invoke_static(tests, "catchArrayStoreBounds", "()I", {});
    if (!array_store_bounds.success) std::cerr << "array store bounds: " << array_store_bounds.error << '\n';
    assert(array_store_bounds.success && std::get<std::int32_t>(array_store_bounds.value) == 15);
    const auto negative = vm.invoke_static(tests, "catchNegativeArraySize", "()I", {});
    if (!negative.success) std::cerr << "negative size exception: " << negative.error << '\n';
    assert(negative.success && std::get<std::int32_t>(negative.value) == 10);
    const auto getfield = vm.invoke_static(tests, "catchNullGetField", "()I", {});
    if (!getfield.success) std::cerr << "getfield exception: " << getfield.error << '\n';
    assert(getfield.success && std::get<std::int32_t>(getfield.value) == 11);
    const auto putfield = vm.invoke_static(tests, "catchNullPutField", "()I", {});
    if (!putfield.success) std::cerr << "putfield exception: " << putfield.error << '\n';
    assert(putfield.success && std::get<std::int32_t>(putfield.value) == 12);
    const auto invoke = vm.invoke_static(tests, "catchNullInvoke", "()I", {});
    if (!invoke.success) std::cerr << "invoke exception: " << invoke.error << '\n';
    assert(invoke.success && std::get<std::int32_t>(invoke.value) == 13);
    const auto diagnostic_result = vm.invoke_static(diagnostic, "run", "()I", {});
    if (!diagnostic_result.success) std::cerr << "throwable diagnostics: " << diagnostic_result.error << '\n';
    assert(diagnostic_result.success && std::get<std::int32_t>(diagnostic_result.value) == 1);
    assert(vm.diagnostics().find("PRINT_STACK_TRACE") != std::string::npos);
    assert(vm.diagnostics().find("java/lang/RuntimeException: menu bootstrap failed") != std::string::npos);
    assert(vm.diagnostics().find("demo/ThrowableDiagnostics.run()I") != std::string::npos);

    const auto cross_frame = vm.invoke_static(boundary, "catchAcrossFrame", "()I", {});
    if (!cross_frame.success) std::cerr << "cross-frame automatic exception: " << cross_frame.error << '\n';
    assert(cross_frame.success && std::get<std::int32_t>(cross_frame.value) == 14);
    assert(vm.diagnostics().find("THROW java/lang/ArithmeticException") != std::string::npos);
    assert(vm.diagnostics().find("demo/ArithmeticWorker.explode") != std::string::npos);

    std::cout << "automatic Java exceptions: arithmetic, null dereference, array load/store bounds, negative size, and cross-frame typed catches PASS\n";
}
