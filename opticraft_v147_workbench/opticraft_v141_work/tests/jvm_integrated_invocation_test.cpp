#include "jvm_class_runtime.hpp"
#include "jvm_object_interpreter.hpp"
#include <cassert>
#include <iostream>
using namespace opti::runtime;

namespace {
ConstantPoolEntry utf(const char* s) { ConstantPoolEntry e; e.tag = 1; e.utf8 = s; return e; }
std::uint16_t add_utf(ParsedClassCode& c, const char* s) { c.constant_pool.push_back(utf(s)); return static_cast<std::uint16_t>(c.constant_pool.size() - 1); }
std::uint16_t add_class(ParsedClassCode& c, const char* name) {
    const auto n = add_utf(c, name); ConstantPoolEntry e; e.tag = 7; e.a = n;
    c.constant_pool.push_back(e); return static_cast<std::uint16_t>(c.constant_pool.size() - 1);
}
std::uint16_t add_nat(ParsedClassCode& c, const char* name, const char* desc) {
    const auto n = add_utf(c, name), d = add_utf(c, desc); ConstantPoolEntry e; e.tag = 12; e.a = n; e.b = d;
    c.constant_pool.push_back(e); return static_cast<std::uint16_t>(c.constant_pool.size() - 1);
}
std::uint16_t add_ref(ParsedClassCode& c, std::uint8_t tag, std::uint16_t owner, std::uint16_t nat) {
    ConstantPoolEntry e; e.tag = tag; e.a = owner; e.b = nat; c.constant_pool.push_back(e);
    return static_cast<std::uint16_t>(c.constant_pool.size() - 1);
}
MethodCode method(std::uint16_t flags, const char* name, const char* desc,
                  std::uint16_t stack, std::uint16_t locals, std::initializer_list<std::uint8_t> code) {
    MethodCode m; m.access_flags = flags; m.name = name; m.descriptor = desc;
    m.max_stack = stack; m.max_locals = locals; m.code.assign(code); return m;
}
}

int main() {
    ParsedClassCode base; base.valid = true; base.this_class = "demo/Base"; base.super_class = "java/lang/Object";
    base.constant_pool.resize(1);
    const auto base_class = add_class(base, "demo/Base");
    const auto field_nat = add_nat(base, "value", "I");
    const auto field_ref = add_ref(base, 9, base_class, field_nat);
    base.fields.push_back({0, "value", "I"});
    base.methods.push_back(method(0, "<init>", "()V", 2, 1,
        {0x2a, 0x10, 5, 0xb5, std::uint8_t(field_ref >> 8), std::uint8_t(field_ref), 0xb1}));
    base.methods.push_back(method(0, "getValue", "()I", 1, 1,
        {0x2a, 0xb4, std::uint8_t(field_ref >> 8), std::uint8_t(field_ref), 0xac}));

    ParsedClassCode derived; derived.valid = true; derived.this_class = "demo/Derived"; derived.super_class = "demo/Base";
    derived.constant_pool.resize(1);
    const auto derived_class = add_class(derived, "demo/Derived");
    (void)derived_class;
    const auto base_ref_class = add_class(derived, "demo/Base");
    const auto init_nat = add_nat(derived, "<init>", "()V");
    const auto base_init_ref = add_ref(derived, 10, base_ref_class, init_nat);
    derived.methods.push_back(method(0, "<init>", "()V", 1, 1,
        {0x2a, 0xb7, std::uint8_t(base_init_ref >> 8), std::uint8_t(base_init_ref), 0xb1}));
    derived.methods.push_back(method(0, "getValue", "()I", 1, 1, {0x10, 9, 0xac}));

    ParsedClassCode utility; utility.valid = true; utility.this_class = "demo/Utility"; utility.super_class = "java/lang/Object";
    utility.constant_pool.resize(1);
    utility.methods.push_back(method(0x0008, "plusOne", "(I)I", 2, 1, {0x1a, 0x04, 0x60, 0xac}));

    ParsedClassCode caller; caller.valid = true; caller.this_class = "demo/Caller"; caller.super_class = "java/lang/Object";
    caller.constant_pool.resize(1);
    const auto caller_derived = add_class(caller, "demo/Derived");
    const auto caller_init_nat = add_nat(caller, "<init>", "()V");
    const auto caller_init_ref = add_ref(caller, 10, caller_derived, caller_init_nat);
    const auto caller_base = add_class(caller, "demo/Base");
    const auto get_nat = add_nat(caller, "getValue", "()I");
    const auto get_ref = add_ref(caller, 10, caller_base, get_nat);
    const auto utility_class = add_class(caller, "demo/Utility");
    const auto plus_nat = add_nat(caller, "plusOne", "(I)I");
    const auto plus_ref = add_ref(caller, 10, utility_class, plus_nat);
    caller.methods.push_back(method(0x0008, "virtualDispatch", "()I", 2, 0,
        {0xbb, std::uint8_t(caller_derived >> 8), std::uint8_t(caller_derived), 0x59,
         0xb7, std::uint8_t(caller_init_ref >> 8), std::uint8_t(caller_init_ref),
         0xb6, std::uint8_t(get_ref >> 8), std::uint8_t(get_ref), 0xac}));
    caller.methods.push_back(method(0x0008, "staticDispatch", "()I", 1, 0,
        {0x10, 40, 0xb8, std::uint8_t(plus_ref >> 8), std::uint8_t(plus_ref), 0xac}));

    ParsedClassCode object_class; object_class.valid = true; object_class.this_class = "java/lang/Object";
    JvmClassRuntime classes;
    assert(classes.register_class(object_class).success);
    assert(classes.register_class(base).success);
    assert(classes.register_class(derived).success);
    assert(classes.register_class(utility).success);
    assert(classes.register_class(caller).success);

    JvmHeap heap; JvmStaticFields statics;
    ObjectBytecodeInterpreter vm(heap, statics, 10000, &classes);
    const auto virtual_result = vm.invoke_static(caller, "virtualDispatch", "()I", {});
    if (!virtual_result.success) std::cerr << "virtual dispatch error: " << virtual_result.error << '\n';
    assert(virtual_result.success && std::get<std::int32_t>(virtual_result.value) == 9);

    const auto static_result = vm.invoke_static(caller, "staticDispatch", "()I", {});
    if (!static_result.success) std::cerr << "static dispatch error: " << static_result.error << '\n';
    assert(static_result.success && std::get<std::int32_t>(static_result.value) == 41);

    // An exception escaping a callee must be offered to the caller's handler.
    ParsedClassCode throwable; throwable.valid = true; throwable.this_class = "java/lang/Throwable";
    throwable.super_class = "java/lang/Object"; throwable.constant_pool.resize(1);
    ParsedClassCode boom; boom.valid = true; boom.this_class = "demo/Boom"; boom.super_class = "java/lang/Throwable";
    boom.constant_pool.resize(1);
    boom.methods.push_back(method(0, "<init>", "()V", 0, 1, {0xb1}));

    ParsedClassCode thrower; thrower.valid = true; thrower.this_class = "demo/Thrower"; thrower.super_class = "java/lang/Object";
    thrower.constant_pool.resize(1);
    const auto boom_class = add_class(thrower, "demo/Boom");
    const auto boom_init_nat = add_nat(thrower, "<init>", "()V");
    const auto boom_init_ref = add_ref(thrower, 10, boom_class, boom_init_nat);
    thrower.methods.push_back(method(0x0008, "throwIt", "()V", 2, 0,
        {0xbb, std::uint8_t(boom_class >> 8), std::uint8_t(boom_class), 0x59,
         0xb7, std::uint8_t(boom_init_ref >> 8), std::uint8_t(boom_init_ref), 0xbf}));

    ParsedClassCode exception_caller; exception_caller.valid = true;
    exception_caller.this_class = "demo/ExceptionCaller"; exception_caller.super_class = "java/lang/Object";
    exception_caller.constant_pool.resize(1);
    const auto thrower_class = add_class(exception_caller, "demo/Thrower");
    const auto throw_nat = add_nat(exception_caller, "throwIt", "()V");
    const auto throw_ref = add_ref(exception_caller, 10, thrower_class, throw_nat);
    const auto catch_class = add_class(exception_caller, "demo/Boom");
    auto catch_method = method(0x0008, "catchAcrossCall", "()I", 1, 0,
        {0xb8, std::uint8_t(throw_ref >> 8), std::uint8_t(throw_ref), 0x04, 0xac, 0x57, 0x05, 0xac});
    catch_method.exception_table.push_back({0, 3, 5, catch_class});
    exception_caller.methods.push_back(catch_method);

    assert(classes.register_class(throwable).success);
    assert(classes.register_class(boom).success);
    assert(classes.register_class(thrower).success);
    assert(classes.register_class(exception_caller).success);
    const auto exception_result = vm.invoke_static(exception_caller, "catchAcrossCall", "()I", {});
    if (!exception_result.success) std::cerr << "cross-call exception error: " << exception_result.error << '\n';
    assert(exception_result.success && std::get<std::int32_t>(exception_result.value) == 2);

    std::cout << "integrated JVM calls: cross-class dispatch, constructors, inherited fields and cross-frame exception handling PASS\n";
}
