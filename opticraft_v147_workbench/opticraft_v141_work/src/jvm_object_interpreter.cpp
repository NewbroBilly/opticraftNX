#include "jvm_object_interpreter.hpp"
#include <png.h>
#include "jvm_class_runtime.hpp"
#include "jvm_exception_runtime.hpp"
#include "lwjgl_gl11_bridge.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <functional>
#include <regex>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <limits>
#include <string>
#include <filesystem>
#include <fstream>
#include <utility>
#include <unordered_set>

namespace opti::runtime {
namespace {
std::uint16_t u2(const std::vector<std::uint8_t>& c, std::size_t p) {
    return (std::uint16_t(c[p]) << 8) | c[p + 1];
}
std::int16_t s2(const std::vector<std::uint8_t>& c, std::size_t p) {
    return static_cast<std::int16_t>(u2(c, p));
}
std::int32_t s4(const std::vector<std::uint8_t>& c, std::size_t p) {
    const std::uint32_t x = (std::uint32_t(c[p]) << 24) |
                            (std::uint32_t(c[p + 1]) << 16) |
                            (std::uint32_t(c[p + 2]) << 8) | c[p + 3];
    return static_cast<std::int32_t>(x);
}

bool parse_desc(const std::string& d, std::vector<std::string>& args, std::string& ret) {
    if (d.empty() || d[0] != '(') return false;
    std::size_t p = 1;
    while (p < d.size() && d[p] != ')') {
        const std::size_t start = p;
        while (p < d.size() && d[p] == '[') ++p;
        if (p >= d.size()) return false;
        if (d[p] == 'L') {
            const auto end = d.find(';', p);
            if (end == std::string::npos) return false;
            p = end + 1;
        } else if (std::string("ZBCSIJFD").find(d[p]) != std::string::npos) {
            ++p;
        } else {
            return false;
        }
        args.push_back(d.substr(start, p - start));
    }
    if (p >= d.size() || d[p++] != ')' || p >= d.size()) return false;
    ret = d.substr(p);
    return ret == "V" || ret == "Z" || ret == "B" || ret == "C" || ret == "S" ||
           ret == "I" || ret == "J" || ret == "F" || ret == "D" ||
           (!ret.empty() && (ret[0] == 'L' || ret[0] == '['));
}
std::size_t slot_width(const std::string& type) {
    return type == "J" || type == "D" ? 2u : 1u;
}
bool as_int(const JvmValue& v, std::int32_t& x) {
    if (const auto* p = std::get_if<std::int32_t>(&v)) { x = *p; return true; }
    return false;
}
bool as_ref(const JvmValue& v, ObjectRef& x) {
    if (std::holds_alternative<NullValue>(v)) { x = 0; return true; }
    if (const auto* p = std::get_if<ObjectRef>(&v)) { x = *p; return true; }
    return false;
}
JvmValue default_value(const std::string& d) {
    if (d == "Z" || d == "B" || d == "C" || d == "S" || d == "I") return std::int32_t(0);
    if (d == "J") return std::int64_t(0);
    if (d == "F") return float(0);
    if (d == "D") return double(0);
    return NullValue{};
}
}

ObjectExecutionResult ObjectBytecodeInterpreter::invoke_static(
    const ParsedClassCode& entry_class, const std::string& name,
    const std::string& descriptor, const std::vector<JvmValue>& args) {
    ObjectExecutionResult output;
    execution_frames_.clear();
    diagnostics_.clear();
    auto fail = [&](const std::string& error) {
        output.error = error;
        output.instructions_executed = 0;
        return output;
    };
    if (!entry_class.valid) return fail("invalid entry classfile");

    const MethodCode* entry = nullptr;
    for (const auto& method : entry_class.methods) {
        if (method.name == name && method.descriptor == descriptor) { entry = &method; break; }
    }
    if (!entry) return fail("entry method not found");
    if (!(entry->access_flags & 0x0008)) return fail("entry method must be static");

    std::size_t executed = 0;
    using ExecResult = std::pair<bool, JvmValue>;
    std::string execution_error;
    ObjectRef pending_exception = 0;
    auto append_diagnostic = [&](const std::string& line) {
        constexpr std::size_t kDiagnosticLimit = 64u * 1024u;
        if (diagnostics_.size() >= kDiagnosticLimit) return;
        const std::size_t available = kDiagnosticLimit - diagnostics_.size();
        diagnostics_.append(line, 0, available);
        if (diagnostics_.size() < kDiagnosticLimit) diagnostics_.push_back('\n');
    };
    std::unordered_set<std::string> initialized_classes;
    std::unordered_set<std::string> initializing_classes;
    // JVM string and class constants are interned per runtime. Reallocating a
    // new String/Class object on every ldc in a render loop quickly exhausted
    // the bounded host heap after the original menu appeared.
    std::unordered_map<std::string, ObjectRef> interned_strings;
    std::unordered_map<std::string, ObjectRef> class_constants;
    std::function<ExecResult(const ParsedClassCode&, const MethodCode&,
                             const std::vector<JvmValue>&, ObjectRef, std::size_t)> execute;

    execute = [&](const ParsedClassCode& cls, const MethodCode& method,
                  const std::vector<JvmValue>& call_args, ObjectRef receiver,
                  std::size_t depth) -> ExecResult {
        auto bad = [&](const std::string& e) -> ExecResult {
            execution_error = e;
            return {false, NullValue{}};
        };
        if (depth > 256) return bad("maximum Java call depth exceeded");
        if (method.code.empty()) return bad("native/abstract/empty method cannot be interpreted: " + cls.this_class + "." + method.name);

        std::vector<std::string> argtypes;
        std::string return_type;
        if (!parse_desc(method.descriptor, argtypes, return_type) || argtypes.size() != call_args.size())
            return bad("invalid method descriptor or argument count: " + method.name + method.descriptor);
        const bool is_static = (method.access_flags & 0x0008) != 0;
        if (is_static && receiver != 0) return bad("static method received an object receiver");
        if (!is_static && receiver == 0) return bad("NullPointerException: instance method receiver");

        std::vector<JvmValue> locals(method.max_locals, NullValue{});
        std::size_t local = 0;
        if (!is_static) {
            if (local >= locals.size()) return bad("instance receiver exceeds max_locals");
            locals[local++] = receiver;
        }
        for (std::size_t i = 0; i < call_args.size(); ++i) {
            if (local >= locals.size()) return bad("arguments exceed max_locals");
            locals[local] = call_args[i];
            local += slot_width(argtypes[i]);
            if (local > locals.size()) return bad("wide argument exceeds max_locals");
        }

        std::vector<JvmValue> stack;
        stack.reserve(method.max_stack);
        execution_frames_.push(JvmExecutionFrame{cls.this_class, method.name, method.descriptor,
                                                  0, locals, stack, receiver, 0, 0});
        struct FramePopGuard {
            JvmExecutionFrameStack& frames;
            ~FramePopGuard() { frames.pop(); }
        } frame_pop_guard{execution_frames_};
        std::size_t pc = 0;
        auto push = [&](const JvmValue& v) {
            if (stack.size() >= method.max_stack) return false;
            stack.push_back(v); return true;
        };
        auto pop = [&](JvmValue& v) {
            if (stack.empty()) return false;
            v = stack.back(); stack.pop_back(); return true;
        };
        auto branch = [&](std::int64_t delta, std::size_t instruction_start) {
            const auto target = static_cast<std::int64_t>(instruction_start) + delta;
            if (target < 0 || target >= static_cast<std::int64_t>(method.code.size())) return false;
            pc = static_cast<std::size_t>(target); return true;
        };
        auto cp_member = [&](std::uint16_t index, std::uint8_t tag,
                             std::string& owner, std::string& member,
                             std::string& desc) -> bool {
            if (!index || index >= cls.constant_pool.size()) return false;
            const auto& ref = cls.constant_pool[index];
            if (ref.tag != tag || !ref.a || !ref.b || ref.a >= cls.constant_pool.size() || ref.b >= cls.constant_pool.size()) return false;
            const auto& class_entry = cls.constant_pool[ref.a];
            const auto& nt = cls.constant_pool[ref.b];
            if (class_entry.tag != 7 || nt.tag != 12 || class_entry.a >= cls.constant_pool.size() ||
                nt.a >= cls.constant_pool.size() || nt.b >= cls.constant_pool.size()) return false;
            owner = cls.constant_pool[class_entry.a].utf8;
            member = cls.constant_pool[nt.a].utf8;
            desc = cls.constant_pool[nt.b].utf8;
            return !owner.empty() && !member.empty() && !desc.empty();
        };
        // Create a Java exception object and route it through this frame's
        // exception table. If this frame cannot catch it, leave it pending so
        // the existing invocation path can unwind into the caller.
        auto raise_java_exception = [&](const std::string& class_name,
                                        const std::string& message,
                                        std::size_t throw_pc) -> bool {
            const ObjectRef exception_ref = heap_.allocate_object(class_name);
            if (!exception_ref) {
                execution_error = message + " (failed to allocate exception object)";
                append_diagnostic("EXCEPTION_ALLOCATION_FAILED " + class_name + ": " + message);
                return false;
            }
            auto* exception_object = heap_.get(exception_ref);
            if (exception_object) {
                exception_object->string_value = message;
                const ObjectRef message_ref = heap_.allocate_string(message);
                if (message_ref) heap_.put_field(exception_ref, "detailMessage", message_ref);
            }
            append_diagnostic("THROW " + class_name + ": " + message + " at " +
                              cls.this_class + "." + method.name + method.descriptor +
                              " pc=" + std::to_string(throw_pc));
            ExceptionHandlerMatch match;
            if (classes_) {
                match = find_exception_handler(cls, method,
                    static_cast<std::uint16_t>(throw_pc), class_name, *classes_);
            } else {
                for (const auto& handler : method.exception_table) {
                    if (throw_pc >= handler.start_pc && throw_pc < handler.end_pc &&
                        handler.catch_type == 0 && handler.handler_pc < method.code.size()) {
                        match = {true, handler.handler_pc, 0};
                        break;
                    }
                }
            }
            if (match.found && match.handler_pc < method.code.size()) {
                stack.clear();
                if (!push(exception_ref)) {
                    execution_error = "exception handler stack overflow";
                    return false;
                }
                pc = match.handler_pc;
                pending_exception = 0;
                execution_error.clear();
                return true;
            }
            pending_exception = exception_ref;
            execution_error = message;
            return false;
        };
        auto resolve_method = [&](const std::string& owner, const std::string& member,
                                  const std::string& desc, InvokeKind kind,
                                  const std::string& receiver_class) -> ResolvedMethod {
            if (classes_) {
                auto resolved = classes_->resolve_method(owner, member, desc, kind, receiver_class);
                if (resolved.method) return resolved;
            }
            if (owner == cls.this_class && (kind != InvokeKind::Virtual || receiver_class == cls.this_class)) {
                for (const auto& candidate : cls.methods) {
                    if (candidate.name == member && candidate.descriptor == desc) {
                        const bool st = (candidate.access_flags & 0x0008) != 0;
                        if ((kind == InvokeKind::Static) == st) return {&cls, &candidate, cls.this_class};
                    }
                }
            }
            return {};
        };

        std::function<bool(const std::string&)> initialize_class;
        initialize_class = [&](const std::string& class_name) -> bool {
            if (!classes_ || initialized_classes.count(class_name) || initializing_classes.count(class_name)) return true;
            const auto* target_class = classes_->find_class(class_name);
            if (!target_class) { initialized_classes.insert(class_name); return true; }
            // The legacy JRE 6 Locale initializer builds a ConcurrentHashMap and
            // reaches sun.misc.Unsafe. The Switch compatibility runtime does not
            // expose that VM-internal API yet; locale-sensitive String intrinsics
            // are deterministic here, so defer Locale's optional global cache.
            if (class_name == "java/util/Locale" || class_name == "java/util/concurrent/atomic/AtomicInteger") {
                initialized_classes.insert(class_name);
                return true;
            }
            // Minecraft 1.2.5's obfuscated gk class builds a 65,536-entry
            // sine lookup table in <clinit>. Running that bytecode through the
            // interpreter adds a large startup penalty, while the table's
            // contents are deterministic and have no observable side effects.
            // Build the same float table directly in the VM heap, then mark the
            // class initialized so ordinary getstatic/faload semantics remain.
            if (class_name == "gk") {
                const auto field = std::find_if(target_class->fields.begin(), target_class->fields.end(),
                    [](const FieldInfo& candidate) { return candidate.name == "a" && candidate.descriptor == "[F" && (candidate.access_flags & 0x0008); });
                if (field != target_class->fields.end()) {
                    const ObjectRef table = heap_.allocate_primitive_array(65536, 'F');
                    if (!table) return false;
                    constexpr double tau = 6.283185307179586476925286766559;
                    for (std::size_t i = 0; i < 65536; ++i) {
                        const float value = static_cast<float>(std::sin(static_cast<double>(i) * tau / 65536.0));
                        if (!heap_.array_store(table, i, value)) return false;
                    }
                    if (!statics_.define(class_name, "a", table)) return false;
                    initialized_classes.insert(class_name);
                    return true;
                }
            }
            if (!target_class->super_class.empty() && target_class->super_class.rfind("java/", 0) != 0 &&
                target_class->super_class.rfind("javax/", 0) != 0 && !initialize_class(target_class->super_class)) return false;
            // JVM ConstantValue attributes are installed before <clinit>. This
            // is required by LWJGL Keyboard's reflective key-constant scan.
            for (const auto& field : target_class->fields) {
                if (!(field.access_flags & 0x0008) || field.constant_value_index == 0 ||
                    field.constant_value_index >= target_class->constant_pool.size()) continue;
                const auto& constant = target_class->constant_pool[field.constant_value_index];
                JvmValue value = default_value(field.descriptor);
                if (constant.tag == 3) value = constant.integer;
                else if (constant.tag == 4) {
                    float number = 0.0f; const std::uint32_t bits = static_cast<std::uint32_t>(constant.integer);
                    std::memcpy(&number, &bits, sizeof(number)); value = number;
                } else if (constant.tag == 5) value = constant.long_integer;
                else if (constant.tag == 6) {
                    double number = 0.0; const std::uint64_t bits = static_cast<std::uint64_t>(constant.long_integer);
                    std::memcpy(&number, &bits, sizeof(number)); value = number;
                } else if (constant.tag == 8 && constant.a < target_class->constant_pool.size() &&
                           target_class->constant_pool[constant.a].tag == 1) {
                    const ObjectRef text_ref = heap_.allocate_string(target_class->constant_pool[constant.a].utf8);
                    if (!text_ref) return false;
                    value = text_ref;
                } else continue;
                if (!statics_.set(class_name, field.name, value) && !statics_.define(class_name, field.name, value)) return false;
            }
            const MethodCode* clinit = nullptr;
            for (const auto& candidate : target_class->methods) {
                if (candidate.name == "<clinit>" && candidate.descriptor == "()V") { clinit = &candidate; break; }
            }
            if (!clinit) { initialized_classes.insert(class_name); return true; }
            initializing_classes.insert(class_name);
            const auto initialized = execute(*target_class, *clinit, {}, 0, depth + 1);
            initializing_classes.erase(class_name);
            if (!initialized.first) return false;
            initialized_classes.insert(class_name);
            return true;
        };

        while (pc < method.code.size()) {
            if (++executed > budget_) return bad("JVM instruction budget exceeded at " + cls.this_class + "." + method.name + method.descriptor + " pc=" + std::to_string(pc));
            execution_frames_.update(pc, locals, stack, pending_exception);
            const std::size_t start = pc;
            const auto op = method.code[pc++];
            auto need = [&](std::size_t count) { return pc <= method.code.size() && count <= method.code.size() - pc; };
            auto load_local = [&](std::size_t index) { return index < locals.size() && push(locals[index]); };
            auto store_local = [&](std::size_t index) {
                JvmValue value;
                if (index >= locals.size() || !pop(value)) return false;
                locals[index] = value; return true;
            };
            JvmValue x, y;
            std::int32_t a = 0, b = 0;
            ObjectRef ref = 0;

            if (op == 0x00) continue; // nop
            if (op == 0x01) { if (!push(NullValue{})) return bad("operand stack overflow");
                continue; }
            if (op >= 0x02 && op <= 0x08) { if (!push(std::int32_t(op) - 3)) return bad("operand stack overflow");
                continue; }
            if (op >= 0x0b && op <= 0x0d) { if (!push(float(op - 0x0b))) return bad("operand stack overflow"); continue; }
            if (op == 0x0e || op == 0x0f) { if (!push(double(op - 0x0e))) return bad("operand stack overflow"); continue; }
            if (op == 0x09 || op == 0x0a) { if (!push(std::int64_t(op - 0x09))) return bad("operand stack overflow"); continue; }
            if (op == 0x10 || op == 0x11) {
                if (op == 0x10) { if (!need(1)) return bad("truncated bipush"); a = static_cast<std::int8_t>(method.code[pc++]); }
                else { if (!need(2)) return bad("truncated sipush"); a = s2(method.code, pc); pc += 2; }
                if (!push(a)) return bad("operand stack overflow");
                continue;
            }
            if (op == 0x12 || op == 0x13) {
                std::uint16_t index;
                if (op == 0x12) { if (!need(1)) return bad("truncated ldc"); index = method.code[pc++]; }
                else { if (!need(2)) return bad("truncated ldc_w"); index = u2(method.code, pc); pc += 2; }
                if (!index || index >= cls.constant_pool.size()) return bad("invalid ldc constant index");
                const auto& constant = cls.constant_pool[index];
                if (constant.tag == 3) {
                    if (!push(constant.integer)) return bad("operand stack overflow");
                } else if (constant.tag == 4) {
                    float value = 0.0f;
                    const std::uint32_t bits = static_cast<std::uint32_t>(constant.integer);
                    static_assert(sizeof(value) == sizeof(bits), "float must be 32-bit");
                    std::memcpy(&value, &bits, sizeof(value));
                    if (!push(value)) return bad("operand stack overflow");
                } else if (constant.tag == 8 && constant.a < cls.constant_pool.size() && cls.constant_pool[constant.a].tag == 1) {
                    const std::string& literal = cls.constant_pool[constant.a].utf8;
                    ObjectRef string_ref = 0;
                    const auto cached = interned_strings.find(literal);
                    if (cached != interned_strings.end()) string_ref = cached->second;
                    else {
                        string_ref = heap_.allocate_string(literal);
                        if (!string_ref) return bad("string constant allocation failed");
                        interned_strings.emplace(literal, string_ref);
                    }
                    if (!push(string_ref)) return bad("operand stack overflow");
                } else if (constant.tag == 7 && constant.a < cls.constant_pool.size() && cls.constant_pool[constant.a].tag == 1) {
                    const std::string& class_name = cls.constant_pool[constant.a].utf8;
                    ObjectRef class_ref = 0;
                    const auto cached = class_constants.find(class_name);
                    if (cached != class_constants.end()) class_ref = cached->second;
                    else {
                        class_ref = heap_.allocate_object("java/lang/Class");
                        auto* class_object = heap_.get(class_ref);
                        if (!class_ref || !class_object) return bad("class constant allocation failed");
                        class_object->string_value = class_name;
                        class_constants.emplace(class_name, class_ref);
                    }
                    if (!push(class_ref)) return bad("operand stack overflow");
                } else return bad("unsupported ldc constant type " + std::to_string(constant.tag) + " in " + cls.this_class + "." + method.name);
                continue;
            }
            if (op == 0x14) { // ldc2_w
                if (!need(2)) return bad("truncated ldc2_w");
                const auto index = u2(method.code, pc); pc += 2;
                if (!index || index >= cls.constant_pool.size()) return bad("invalid ldc2_w index");
                const auto& constant = cls.constant_pool[index];
                if (constant.tag == 5) { if (!push(constant.long_integer)) return bad("operand stack overflow"); }
                else if (constant.tag == 6) {
                    double value = 0.0; const std::uint64_t bits = static_cast<std::uint64_t>(constant.long_integer);
                    std::memcpy(&value, &bits, sizeof(value));
                    if (!push(value)) return bad("operand stack overflow");
                } else return bad("ldc2_w requires long or double constant");
                continue;
            }
            if (op == 0x15 || op == 0x16 || op == 0x17 || op == 0x18 || op == 0x19 || op == 0x36 || op == 0x37 || op == 0x38 || op == 0x39 || op == 0x3a) {
                if (!need(1)) return bad("truncated local load/store");
                const auto index = method.code[pc++];
                if (op == 0x15 || op == 0x16 || op == 0x17 || op == 0x18 || op == 0x19) { if (!load_local(index)) return bad("invalid local load"); }
                else if (!store_local(index)) return bad("invalid local store");
                continue;
            }
            if ((op >= 0x1a && op <= 0x1d) || (op >= 0x1e && op <= 0x21) || (op >= 0x22 && op <= 0x25) || (op >= 0x26 && op <= 0x29) || (op >= 0x2a && op <= 0x2d)) {
                const auto index = op <= 0x1d ? op - 0x1a : (op <= 0x21 ? op - 0x1e : (op <= 0x25 ? op - 0x22 : (op <= 0x29 ? op - 0x26 : op - 0x2a)));
                if (!load_local(index)) return bad("invalid compact local load");
                continue;
            }
            if ((op >= 0x3b && op <= 0x3e) || (op >= 0x3f && op <= 0x42) || (op >= 0x43 && op <= 0x46) || (op >= 0x47 && op <= 0x4a) || (op >= 0x4b && op <= 0x4e)) {
                const auto index = op <= 0x3e ? op - 0x3b : (op <= 0x42 ? op - 0x3f : (op <= 0x46 ? op - 0x43 : (op <= 0x4a ? op - 0x47 : op - 0x4b)));
                if (!store_local(index)) return bad("invalid compact local store");
                continue;
            }
            if (op == 0x59) { // dup: ..., value1 -> ..., value1, value1
                if (stack.empty() || std::holds_alternative<std::int64_t>(stack.back()) ||
                    std::holds_alternative<double>(stack.back()) || !push(stack.back()))
                    return bad("dup requires one category-1 value");
                continue;
            }
            if (op == 0x5a) { // dup_x1
                if (stack.size() < 2) return bad("dup_x1 stack underflow");
                const auto v1 = stack.back(); const auto v2 = stack[stack.size() - 2];
                if (std::holds_alternative<std::int64_t>(v1) || std::holds_alternative<double>(v1) ||
                    std::holds_alternative<std::int64_t>(v2) || std::holds_alternative<double>(v2) ||
                    stack.size() + 1 > method.max_stack) return bad("invalid dup_x1 form/stack overflow");
                stack.pop_back(); stack.pop_back();
                if (!push(v1) || !push(v2) || !push(v1)) return bad("dup_x1 stack overflow");
                continue;
            }
            if (op == 0x5b) { // dup_x2, both JVM forms
                if (stack.size() < 2) return bad("dup_x2 stack underflow");
                const auto v1 = stack.back();
                if (std::holds_alternative<std::int64_t>(v1) || std::holds_alternative<double>(v1))
                    return bad("dup_x2 top value must be category-1");
                const auto v2 = stack[stack.size() - 2];
                stack.pop_back(); stack.pop_back();
                if (std::holds_alternative<std::int64_t>(v2) || std::holds_alternative<double>(v2)) {
                    if (!push(v1) || !push(v2) || !push(v1)) return bad("dup_x2 stack overflow");
                } else {
                    if (stack.empty()) return bad("dup_x2 missing third value");
                    const auto v3 = stack.back();
                    if (std::holds_alternative<std::int64_t>(v3) || std::holds_alternative<double>(v3))
                        return bad("invalid dup_x2 category form");
                    stack.pop_back();
                    if (!push(v1) || !push(v3) || !push(v2) || !push(v1)) return bad("dup_x2 stack overflow");
                }
                continue;
            }
            if (op == 0x5c) { // dup2
                if (stack.empty()) return bad("dup2 stack underflow");
                const auto v1 = stack.back();
                if (std::holds_alternative<std::int64_t>(v1) || std::holds_alternative<double>(v1)) {
                    if (!push(v1)) return bad("dup2 stack overflow");
                } else {
                    if (stack.size() < 2) return bad("dup2 stack underflow");
                    const auto v2 = stack[stack.size() - 2];
                    if (std::holds_alternative<std::int64_t>(v2) || std::holds_alternative<double>(v2))
                        return bad("invalid dup2 category form");
                    if (stack.size() + 2 > method.max_stack || !push(v2) || !push(v1))
                        return bad("dup2 stack overflow");
                }
                continue;
            }
            if (op == 0x5f) { // swap
                if (stack.size() < 2) return bad("swap stack underflow");
                if (std::holds_alternative<std::int64_t>(stack.back()) || std::holds_alternative<double>(stack.back()) ||
                    std::holds_alternative<std::int64_t>(stack[stack.size() - 2]) ||
                    std::holds_alternative<double>(stack[stack.size() - 2]))
                    return bad("swap requires category-1 values");
                std::swap(stack[stack.size() - 1], stack[stack.size() - 2]);
                continue;
            }
            if (op == 0x57) { if (!pop(x)) return bad("pop underflow");
                if (std::holds_alternative<std::int64_t>(x) || std::holds_alternative<double>(x))
                    return bad("pop cannot consume category-2 value");
                continue; }
            if (op == 0x63 || op == 0x67 || op == 0x6b || op == 0x6f || op == 0x73) {
                if (!pop(y) || !pop(x) || !std::holds_alternative<double>(x) || !std::holds_alternative<double>(y))
                    return bad("double arithmetic type/stack error");
                const double lhs = std::get<double>(x), rhs = std::get<double>(y);
                double result = 0.0;
                if (op == 0x63) result = lhs + rhs;
                else if (op == 0x67) result = lhs - rhs;
                else if (op == 0x6b) result = lhs * rhs;
                else if (op == 0x6f) result = lhs / rhs;
                else result = std::fmod(lhs, rhs);
                if (!push(result)) return bad("operand stack overflow");
                continue;
            }
            if (op == 0x62 || op == 0x66 || op == 0x6a || op == 0x6e || op == 0x72) {
                if (!pop(y) || !pop(x) || !std::holds_alternative<float>(x) || !std::holds_alternative<float>(y))
                    return bad("float arithmetic type/stack error");
                const float lhs = std::get<float>(x), rhs = std::get<float>(y);
                float result = 0.0f;
                if (op == 0x62) result = lhs + rhs;
                else if (op == 0x66) result = lhs - rhs;
                else if (op == 0x6a) result = lhs * rhs;
                else if (op == 0x6e) result = lhs / rhs;
                else result = std::fmod(lhs, rhs);
                if (!push(result)) return bad("operand stack overflow");
                continue;
            }
            if (op == 0x60 || op == 0x64 || op == 0x68 || op == 0x6c || op == 0x70) {
                if (!pop(y) || !pop(x) || !as_int(x, a) || !as_int(y, b)) return bad("integer arithmetic type/stack error");
                if ((op == 0x6c || op == 0x70) && b == 0) {
                    if (raise_java_exception("java/lang/ArithmeticException", "ArithmeticException: division by zero", start)) continue;
                    return {false, NullValue{}};
                }
                if (op == 0x60) a = static_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
                else if (op == 0x64) a = static_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
                else if (op == 0x68) a = static_cast<std::int32_t>(std::uint32_t(a) * std::uint32_t(b));
                else if (op == 0x70) {
                    if (a == std::numeric_limits<std::int32_t>::min() && b == -1) a = 0;
                    else a %= b;
                } else if (a == std::numeric_limits<std::int32_t>::min() && b == -1) a = a;
                else a /= b;
                if (!push(a)) return bad("operand stack overflow");
                continue;
            }
            if (op == 0x61 || op == 0x65 || op == 0x69 || op == 0x6d || op == 0x71) {
                if (!pop(y) || !pop(x) || !std::holds_alternative<std::int64_t>(x) || !std::holds_alternative<std::int64_t>(y)) return bad("long arithmetic type/stack error");
                const auto lhs=std::get<std::int64_t>(x), rhs=std::get<std::int64_t>(y);
                if ((op == 0x6d || op == 0x71) && rhs == 0) { if (raise_java_exception("java/lang/ArithmeticException", "long division by zero", start)) continue; return {false,NullValue{}}; }
                std::int64_t value = 0;
                if (op == 0x61) value = static_cast<std::int64_t>(static_cast<std::uint64_t>(lhs) + static_cast<std::uint64_t>(rhs));
                else if (op == 0x65) value = static_cast<std::int64_t>(static_cast<std::uint64_t>(lhs) - static_cast<std::uint64_t>(rhs));
                else if (op == 0x69) value = static_cast<std::int64_t>(static_cast<std::uint64_t>(lhs) * static_cast<std::uint64_t>(rhs));
                else if (op == 0x71) value = (lhs == std::numeric_limits<std::int64_t>::min() && rhs == -1) ? 0 : lhs % rhs;
                else value = (lhs == std::numeric_limits<std::int64_t>::min() && rhs == -1) ? lhs : lhs / rhs;
                if (!push(value)) return bad("operand stack overflow");
                continue;
            }
            if (op == 0x74 || op == 0x75) {
                if (!pop(x)) return bad("negation stack underflow");
                if (op == 0x74 && std::holds_alternative<std::int32_t>(x)) x = static_cast<std::int32_t>(-static_cast<std::uint32_t>(std::get<std::int32_t>(x)));
                else if (op == 0x75 && std::holds_alternative<std::int64_t>(x)) x = static_cast<std::int64_t>(-static_cast<std::uint64_t>(std::get<std::int64_t>(x)));
                else return bad("negation type mismatch");
                if (!push(x)) return bad("operand stack overflow");
                continue;
            }
            if (op == 0x76 || op == 0x77) {
                if (!pop(x)) return bad("floating negation stack underflow");
                if (op == 0x76 && std::holds_alternative<float>(x)) x = -std::get<float>(x);
                else if (op == 0x77 && std::holds_alternative<double>(x)) x = -std::get<double>(x);
                else return bad("floating negation type mismatch");
                if (!push(x)) return bad("operand stack overflow");
                continue;
            }
            if (op == 0x78 || op == 0x7a || op == 0x7c || op == 0x7e || op == 0x80 || op == 0x82) {
                if (!pop(y) || !pop(x) || !as_int(x, a) || !as_int(y, b)) return bad("integer bitwise/shift type error");
                const unsigned shift = static_cast<unsigned>(b) & 31u;
                const std::int32_t value = op == 0x78 ? static_cast<std::int32_t>(static_cast<std::uint32_t>(a) << shift) :
                    op == 0x7a ? static_cast<std::int32_t>(a >> shift) :
                    op == 0x7c ? static_cast<std::int32_t>(static_cast<std::uint32_t>(a) >> shift) :
                    op == 0x7e ? (a & b) : op == 0x80 ? (a | b) : (a ^ b);
                if (!push(value)) return bad("operand stack overflow");
                continue;
            }
            if (op == 0x79 || op == 0x7b || op == 0x7d) {
                if (!pop(y) || !pop(x) || !std::holds_alternative<std::int64_t>(x) || !as_int(y, b)) return bad("long shift type error");
                const auto lhs = std::get<std::int64_t>(x); const unsigned shift = static_cast<unsigned>(b) & 63u;
                const std::int64_t value = op == 0x79 ? static_cast<std::int64_t>(static_cast<std::uint64_t>(lhs) << shift) :
                    op == 0x7b ? lhs >> shift : static_cast<std::int64_t>(static_cast<std::uint64_t>(lhs) >> shift);
                if (!push(value)) return bad("operand stack overflow");
                continue;
            }
            if (op == 0x7f || op == 0x81 || op == 0x83) {
                if (!pop(y) || !pop(x) || !std::holds_alternative<std::int64_t>(x) || !std::holds_alternative<std::int64_t>(y)) return bad("long bitwise type error");
                const auto lhs = std::get<std::int64_t>(x), rhs = std::get<std::int64_t>(y);
                const auto value = op == 0x7f ? (lhs & rhs) : op == 0x81 ? (lhs | rhs) : (lhs ^ rhs);
                if (!push(value)) return bad("operand stack overflow");
                continue;
            }
            if (op >= 0x85 && op <= 0x90) {
                if (!pop(x)) return bad("numeric conversion stack underflow");
                JvmValue converted;
                bool supported = true;
                if (op == 0x85 && std::holds_alternative<std::int32_t>(x)) converted = static_cast<std::int64_t>(std::get<std::int32_t>(x));
                else if (op == 0x86 && std::holds_alternative<std::int32_t>(x)) converted = static_cast<float>(std::get<std::int32_t>(x));
                else if (op == 0x87 && std::holds_alternative<std::int32_t>(x)) converted = static_cast<double>(std::get<std::int32_t>(x));
                else if (op == 0x88 && std::holds_alternative<std::int64_t>(x)) converted = static_cast<std::int32_t>(std::get<std::int64_t>(x));
                else if (op == 0x89 && std::holds_alternative<std::int64_t>(x)) converted = static_cast<float>(std::get<std::int64_t>(x));
                else if (op == 0x8a && std::holds_alternative<std::int64_t>(x)) converted = static_cast<double>(std::get<std::int64_t>(x));
                else if (op == 0x8b && std::holds_alternative<float>(x)) converted = static_cast<std::int32_t>(std::get<float>(x));
                else if (op == 0x8c && std::holds_alternative<float>(x)) converted = static_cast<std::int64_t>(std::get<float>(x));
                else if (op == 0x8d && std::holds_alternative<float>(x)) converted = static_cast<double>(std::get<float>(x));
                else if (op == 0x8e && std::holds_alternative<double>(x)) converted = static_cast<std::int32_t>(std::get<double>(x));
                else if (op == 0x8f && std::holds_alternative<double>(x)) converted = static_cast<std::int64_t>(std::get<double>(x));
                else if (op == 0x90 && std::holds_alternative<double>(x)) converted = static_cast<float>(std::get<double>(x));
                else supported = false;
                if (!supported) return bad("unsupported numeric conversion type");
                if (!push(converted)) return bad("operand stack overflow");
                continue;
            }
            if (op >= 0x91 && op <= 0x93) {
                if (!pop(x) || !std::holds_alternative<std::int32_t>(x)) return bad("integer narrowing conversion type error");
                const auto value=std::get<std::int32_t>(x);
                const std::int32_t converted = op==0x91 ? static_cast<std::int8_t>(value) : (op==0x92 ? static_cast<std::uint16_t>(value) : static_cast<std::int16_t>(value));
                if(!push(converted))return bad("integer narrowing conversion stack overflow");
                continue;
            }
            if (op == 0x94) { // lcmp
                if (!pop(y) || !pop(x) || !std::holds_alternative<std::int64_t>(x) ||
                    !std::holds_alternative<std::int64_t>(y)) return bad("lcmp stack/type error");
                const auto left = std::get<std::int64_t>(x), right = std::get<std::int64_t>(y);
                if (!push(std::int32_t(left < right ? -1 : left > right ? 1 : 0))) return bad("lcmp stack overflow");
                continue;
            }
            if (op == 0x95 || op == 0x96) {
                if (!pop(y) || !pop(x) || !std::holds_alternative<float>(x) || !std::holds_alternative<float>(y)) return bad("float compare type/stack error");
                const float lhs=std::get<float>(x),rhs=std::get<float>(y);
                const int result=(std::isnan(lhs)||std::isnan(rhs))?(op==0x95?-1:1):(lhs>rhs?1:(lhs==rhs?0:-1));
                if (!push(static_cast<std::int32_t>(result))) return bad("operand stack overflow");
                continue;
            }
            if (op == 0x97 || op == 0x98) {
                if (!pop(y) || !pop(x) || !std::holds_alternative<double>(x) || !std::holds_alternative<double>(y)) return bad("double compare type/stack error");
                const double lhs=std::get<double>(x),rhs=std::get<double>(y);
                const int result=(std::isnan(lhs)||std::isnan(rhs))?(op==0x97?-1:1):(lhs>rhs?1:(lhs==rhs?0:-1));
                if (!push(static_cast<std::int32_t>(result))) return bad("operand stack overflow");
                continue;
            }
            if (op == 0x71) { // lrem
                if (!pop(y) || !pop(x) || !std::holds_alternative<std::int64_t>(x) ||
                    !std::holds_alternative<std::int64_t>(y)) return bad("lrem stack/type error");
                const auto left = std::get<std::int64_t>(x), right = std::get<std::int64_t>(y);
                if (right == 0) {
                    if (raise_java_exception("java/lang/ArithmeticException", "ArithmeticException: long remainder by zero", start)) continue;
                    return {false, NullValue{}};
                }
                const auto result = (left == std::numeric_limits<std::int64_t>::min() && right == -1) ? 0 : left % right;
                if (!push(static_cast<std::int64_t>(result))) return bad("operand stack overflow");
                continue;
            }
            if (op == 0x84) {
                if (!need(2)) return bad("truncated iinc");
                const auto index = method.code[pc++]; const auto amount = static_cast<std::int8_t>(method.code[pc++]);
                if (index >= locals.size() || !as_int(locals[index], a)) return bad("invalid iinc local");
                locals[index] = static_cast<std::int32_t>(std::uint32_t(a) + static_cast<std::uint32_t>(amount));
                continue;
            }
            if (op == 0xc4) { // wide local-variable instructions
                if (!need(1)) return bad("truncated wide opcode");
                const auto sub = method.code[pc++];
                if (!need(2)) return bad("truncated wide local index");
                const auto index = u2(method.code, pc); pc += 2;
                if (sub == 0x84) {
                    if (!need(2)) return bad("truncated wide iinc");
                    const auto amount = s2(method.code, pc); pc += 2;
                    if (index >= locals.size() || !as_int(locals[index], a)) return bad("invalid wide iinc local");
                    locals[index] = static_cast<std::int32_t>(std::uint32_t(a) + static_cast<std::uint32_t>(static_cast<std::int32_t>(amount)));
                } else if (sub == 0x15 || sub == 0x16 || sub == 0x17 || sub == 0x18 || sub == 0x19) {
                    if (!load_local(index)) return bad("invalid wide local load");
                } else if (sub == 0x36 || sub == 0x37 || sub == 0x38 || sub == 0x39 || sub == 0x3a) {
                    if (!store_local(index)) return bad("invalid wide local store");
                } else {
                    return bad("unsupported wide local opcode " + std::to_string(sub));
                }
                continue;
            }
            if (op >= 0x99 && op <= 0x9e) {
                if (!need(2) || !pop(x) || !as_int(x, a)) return bad("invalid integer branch in " + cls.this_class + "." + method.name + method.descriptor + " opcode=" + std::to_string(op) + " pc=" + std::to_string(start) + " stack_type=" + std::to_string(x.index()) + " stack_size=" + std::to_string(stack.size()));
                const int delta = s2(method.code, pc); pc += 2;
                const bool take = op == 0x99 ? a == 0 : op == 0x9a ? a != 0 : op == 0x9b ? a < 0 : op == 0x9c ? a >= 0 : op == 0x9d ? a > 0 : a <= 0;
                if (take && !branch(delta, start)) return bad("invalid branch target");
                continue;
            }
            if (op >= 0x9f && op <= 0xa4) {
                if (!need(2) || !pop(y) || !pop(x) || !as_int(x, a) || !as_int(y, b)) return bad("invalid integer compare branch in " + cls.this_class + "." + method.name + " opcode=" + std::to_string(op));
                const int delta = s2(method.code, pc); pc += 2;
                const bool take = op == 0x9f ? a == b : op == 0xa0 ? a != b : op == 0xa1 ? a < b : op == 0xa2 ? a >= b : op == 0xa3 ? a > b : a <= b;
                if (take && !branch(delta, start)) return bad("invalid branch target");
                continue;
            }
            if (op == 0xa5 || op == 0xa6) {
                if (!need(2) || !pop(y) || !pop(x)) return bad("invalid reference comparison branch");
                ObjectRef left = 0, right = 0;
                if (!as_ref(x, left) || !as_ref(y, right)) return bad("reference comparison received non-reference");
                const int delta = s2(method.code, pc); pc += 2;
                const bool take = op == 0xa5 ? left == right : left != right;
                if (take && !branch(delta, start)) return bad("invalid reference branch target");
                continue;
            }
            if (op == 0xc6 || op == 0xc7) {
                if (!need(2) || !pop(x)) return bad("invalid null branch");
                ObjectRef value = 0;
                if (!as_ref(x, value)) return bad("null branch received non-reference");
                const int delta = s2(method.code, pc); pc += 2;
                const bool take = op == 0xc6 ? value == 0 : value != 0;
                if (take && !branch(delta, start)) return bad("invalid null branch target");
                continue;
            }
            if (op == 0xc8) {
                if (!need(4)) return bad("truncated goto_w");
                const auto delta = s4(method.code, pc); pc += 4;
                if (!branch(delta, start)) return bad("invalid goto_w target");
                continue;
            }
            if (op == 0xa7) {
                if (!need(2)) return bad("truncated goto");
                const int delta = s2(method.code, pc); pc += 2;
                if (!branch(delta, start)) return bad("invalid goto target");
                continue;
            }
            if (op == 0xaa || op == 0xab) {
                while ((pc & 3u) != 0u) { if (!need(1)) return bad("truncated switch padding"); ++pc; }
                if (!need(op == 0xaa ? 12 : 8)) return bad("truncated switch");
                const auto default_delta = s4(method.code, pc); pc += 4;
                std::int32_t selected = default_delta;
                if (op == 0xaa) {
                    const auto low = s4(method.code, pc); const auto high = s4(method.code, pc + 4); pc += 8;
                    const auto count64 = std::int64_t(high) - std::int64_t(low) + 1;
                    if (count64 <= 0 || std::uint64_t(count64) > (method.code.size() / 4) ||
                        std::uint64_t(count64) * 4 > method.code.size() - pc) return bad("invalid tableswitch bounds");
                    const auto count = static_cast<std::size_t>(count64);
                    if (!pop(x) || !as_int(x, a)) return bad("tableswitch key is not int");
                    if (a >= low && a <= high) selected = s4(method.code, pc + std::size_t(std::int64_t(a) - low) * 4);
                    pc += count * 4;
                } else {
                    const auto pairs = s4(method.code, pc); pc += 4;
                    if (pairs < 0 || std::uint64_t(pairs) > method.code.size() / 8 || !need(std::size_t(pairs) * 8)) return bad("invalid lookupswitch pair count");
                    if (!pop(x) || !as_int(x, a)) return bad("lookupswitch key is not int");
                    for (std::int32_t i = 0; i < pairs; ++i) { const auto key = s4(method.code, pc); const auto delta = s4(method.code, pc + 4); if (key == a) selected = delta; pc += 8; }
                }
                if (!branch(selected, start)) return bad("invalid switch target");
                continue;
            }
            if (op == 0xc0 || op == 0xc1) { // checkcast / instanceof
                if (!need(2)) return bad("truncated type check");
                const auto ci = u2(method.code, pc); pc += 2;
                if (!ci || ci >= cls.constant_pool.size() || cls.constant_pool[ci].tag != 7 ||
                    cls.constant_pool[ci].a >= cls.constant_pool.size())
                    return bad("invalid class constant for type check");
                const auto& name_entry = cls.constant_pool[cls.constant_pool[ci].a];
                const std::string target_name = name_entry.utf8;
                if (target_name.empty()) return bad("empty class name in type check");
                if (!pop(x)) return bad("type check stack underflow");
                ObjectRef object_ref = 0;
                if (!as_ref(x, object_ref)) return bad("type check requires reference");
                bool matches = false;
                if (object_ref) {
                    const auto* object = heap_.get(object_ref);
                    if (!object) return bad("type check on invalid reference");
                    if (object->kind == HeapKind::Object) {
                        matches = object->class_name == target_name ||
                                  (classes_ && classes_->is_assignable(object->class_name, target_name));
                    } else {
                        matches = target_name == "java/lang/Object" || target_name == "java/lang/Cloneable" ||
                                  target_name == "java/io/Serializable" || object->class_name == target_name;
                    }
                }
                if (op == 0xc0) {
                    if (!matches && object_ref) return bad("ClassCastException: " + target_name);
                    if (!push(x)) return bad("operand stack overflow after checkcast");
                } else {
                    if (!push(std::int32_t(matches ? 1 : 0))) return bad("operand stack overflow after instanceof");
                }
                continue;
            }
            if (op == 0xbf) { // athrow; transfer to this method's matching handler
                if (!pop(x) || !as_ref(x, ref)) return bad("athrow requires a Throwable reference");
                if (!ref) {
                    if (raise_java_exception("java/lang/NullPointerException", "NullPointerException: athrow null", start)) continue;
                    return {false, NullValue{}};
                }
                const auto* thrown = heap_.get(ref);
                if (!thrown || thrown->kind != HeapKind::Object) return bad("athrow on invalid object");
                bool handled = false;
                if (classes_) {
                    const auto match = find_exception_handler(cls, method,
                        static_cast<std::uint16_t>(start), thrown->class_name, *classes_);
                    if (match.found && match.handler_pc < method.code.size()) {
                        stack.clear();
                        if (!push(ref)) return bad("exception handler stack overflow");
                        pc = match.handler_pc;
                        handled = true;
                    }
                } else {
                    for (const auto& handler : method.exception_table) {
                        if (start >= handler.start_pc && start < handler.end_pc &&
                            handler.catch_type == 0 && handler.handler_pc < method.code.size()) {
                            stack.clear();
                            if (!push(ref)) return bad("exception handler stack overflow");
                            pc = handler.handler_pc;
                            handled = true;
                            break;
                        }
                    }
                }
                if (!handled) {
                    pending_exception = ref;
                    return bad("uncaught Java exception: " + thrown->class_name);
                }
                continue;
            }
            if (op == 0xbb) {
                if (!need(2)) return bad("truncated new");
                const auto ci = u2(method.code, pc); pc += 2;
                if (!ci || ci >= cls.constant_pool.size() || cls.constant_pool[ci].tag != 7 || cls.constant_pool[ci].a >= cls.constant_pool.size()) return bad("invalid new class reference");
                const std::string class_name = cls.constant_pool[cls.constant_pool[ci].a].utf8;
                std::vector<FieldLayout> layout;
                ObjectRef id = 0;
                if (classes_ && classes_->find_class(class_name)) {
                    std::string layout_error;
                    if (!classes_->build_instance_layout(class_name, layout, layout_error)) return bad(layout_error);
                    id = heap_.allocate_instance(class_name, layout);
                } else if (class_name == cls.this_class) {
                    for (const auto& f : cls.fields) if (!(f.access_flags & 0x0008)) layout.push_back({class_name + "." + f.name, f.descriptor, false});
                    id = layout.empty() ? heap_.allocate_object(class_name) : heap_.allocate_instance(class_name, layout);
                } else {
                    id = heap_.allocate_object(class_name);
                }
                if (!id || !push(id)) return bad("object allocation failed");
                continue;
            }
            if (op == 0xb2 || op == 0xb3 || op == 0xb4 || op == 0xb5) {
                if (!need(2)) return bad("truncated field access");
                const auto ci = u2(method.code, pc); pc += 2;
                std::string owner, field, fd;
                if (!cp_member(ci, 9, owner, field, fd)) return bad("invalid Fieldref constant");
                std::string static_owner = owner;
                if (op == 0xb2 || op == 0xb3) {
                    // Resolve inherited static fields to the class that actually
                    // declares them. Using the constant-pool symbolic owner as
                    // the storage key creates a second, null copy of superclass fields.
                    std::string current = owner;
                    std::unordered_set<std::string> seen_static;
                    bool resolved = false;
                    while (classes_ && !current.empty() && seen_static.insert(current).second) {
                        const auto* declaring = classes_->find_class(current);
                        if (!declaring) break;
                        for (const auto& candidate : declaring->fields) {
                            if (candidate.name == field && candidate.descriptor == fd &&
                                (candidate.access_flags & 0x0008)) {
                                static_owner = current;
                                resolved = true;
                                break;
                            }
                        }
                        if (resolved) break;
                        current = declaring->super_class;
                    }
                    if (!initialize_class(static_owner)) return {false, NullValue{}};
                }
                if (op == 0xb2) {
                    if (static_owner == "java/lang/System" && (field == "out" || field == "err") &&
                        fd == "Ljava/io/PrintStream;") {
                        if (!statics_.get(static_owner, field, x) || std::holds_alternative<NullValue>(x)) {
                            const ObjectRef stream = heap_.allocate_object("java/io/PrintStream");
                            if (!stream) return bad("System PrintStream allocation failed");
                            x = stream;
                            if (!statics_.define(static_owner, field, x) && !statics_.set(static_owner, field, x)) return bad("System PrintStream static field setup failed");
                        }
                    } else if (!statics_.get(static_owner, field, x)) {
                        statics_.define(static_owner, field, default_value(fd));
                        if (!statics_.get(static_owner, field, x)) return bad("static field initialization failed");
                    }
                    if (!push(x)) return bad("operand stack overflow");
                continue;
                }
                if (op == 0xb3) {
                    if (!pop(x)) return bad("putstatic stack underflow: " + owner + "." + field);
                    if (!statics_.set(static_owner, field, x)) {
                        statics_.define(static_owner, field, default_value(fd));
                        if (!statics_.set(static_owner, field, x)) return bad("invalid putstatic: " + static_owner + "." + field);
                    }
                    continue;
                }
                if (op == 0xb4) {
                    if (!pop(x) || !as_ref(x, ref)) return bad("getfield stack/type error");
                    if (!ref) {
                        if (raise_java_exception("java/lang/NullPointerException", "NullPointerException: getfield " + cls.this_class + "." + method.name + " -> " + owner + "." + field, start)) continue;
                        return {false, NullValue{}};
                    }
                    // JVM field references are resolved from the symbolic owner
                    // through its superclass chain. A suffix-only lookup is unsafe:
                    // unrelated classes may each declare a field named "a".
                    std::string resolved_key;
                    std::string current = owner;
                    std::unordered_set<std::string> seen_fields;
                    while (!current.empty() && seen_fields.insert(current).second) {
                        const std::string candidate = current + "." + field;
                        if (heap_.get_field(ref, candidate, y)) { resolved_key = candidate; break; }
                        const auto* declaring = classes_ ? classes_->find_class(current) : nullptr;
                        if (!declaring) break;
                        current = declaring->super_class;
                    }
                    if (resolved_key.empty() && heap_.get_field(ref, field, y)) resolved_key = field;
                    // LWJGL 2 ContextCapabilities is a generated data carrier whose
                    // hundreds of final boolean fields are normally initialized by
                    // native GL capability discovery. The Switch bridge advertises
                    // a deliberately conservative OpenGL 1.1 profile, so answer
                    // those generated capability reads without fabricating newer GL.
                    if (resolved_key.empty() && owner == "org/lwjgl/opengl/ContextCapabilities") {
                        if (fd == "J" && field.rfind("gl", 0) == 0) {
                            // The software bridge exposes a conservative GL 1.1 surface;
                            // function-pointer presence is represented by a nonzero token.
                            y = std::int64_t(1);
                            if (!push(y)) return bad("LWJGL function capability stack overflow");
                            continue;
                        }
                        bool supported = false;
                        if (field == "OpenGL11") supported = (gl_bridge_ != nullptr);
                        else if (field == "OpenGL12" || field == "OpenGL13" ||
                                 field == "OpenGL14" || field == "OpenGL15" ||
                                 field == "OpenGL20" || field == "OpenGL21" ||
                                 field == "OpenGL30" || field == "OpenGL31" ||
                                 field == "OpenGL32" || field == "OpenGL33" ||
                                 field == "OpenGL40" || field == "OpenGL41" ||
                                 field == "OpenGL42" || field == "OpenGL43" ||
                                 field == "OpenGL44" || field == "OpenGL45" ||
                                 field == "OpenGL46") supported = false;
                        y = std::int32_t(supported ? 1 : 0);
                        if (fd != "Z") return bad("unsupported non-boolean LWJGL capability field: " + field);
                        if (!push(y)) return bad("operand stack overflow");
                        continue;
                    }
                    if (resolved_key.empty()) return bad("unknown instance field: " + owner + "." + field);
                    if (!push(y)) return bad("operand stack overflow");
                continue;
                }
                if (!pop(y) || !pop(x) || !as_ref(x, ref)) return bad("putfield stack/type error");
                if (!ref) {
                    if (raise_java_exception("java/lang/NullPointerException", "NullPointerException: putfield", start)) continue;
                    return {false, NullValue{}};
                }
                std::string resolved_key;
                std::string current = owner;
                std::unordered_set<std::string> seen_fields;
                JvmValue existing;
                while (!current.empty() && seen_fields.insert(current).second) {
                    const std::string candidate = current + "." + field;
                    if (heap_.get_field(ref, candidate, existing)) { resolved_key = candidate; break; }
                    const auto* declaring = classes_ ? classes_->find_class(current) : nullptr;
                    if (!declaring) break;
                    current = declaring->super_class;
                }
                if (resolved_key.empty() && heap_.get_field(ref, field, existing)) resolved_key = field;
                if (resolved_key.empty() || !heap_.put_field(ref, resolved_key, y))
                    return bad("invalid putfield: " + owner + "." + field);
                continue;
            }
            if (op == 0xb6 || op == 0xb7 || op == 0xb8 || op == 0xb9) {
                std::uint16_t ci = 0;
                if (op == 0xb9) {
                    if (!need(4)) return bad("truncated invokeinterface");
                    ci = u2(method.code, pc); pc += 2;
                    const auto count = method.code[pc++]; const auto zero = method.code[pc++];
                    if (count == 0 || zero != 0) return bad("malformed invokeinterface operands");
                } else {
                    if (!need(2)) return bad("truncated method invocation");
                    ci = u2(method.code, pc); pc += 2;
                }
                std::string owner, target_name, target_desc;
                const auto tag = (op == 0xb9) ? 11 : 10;
                if (!cp_member(ci, tag, owner, target_name, target_desc)) return bad("invalid method reference constant");
                std::vector<std::string> target_types; std::string target_return;
                if (!parse_desc(target_desc, target_types, target_return)) return bad("invalid invoked method descriptor");
                std::vector<JvmValue> target_args(target_types.size());
                for (std::size_t i = target_types.size(); i > 0; --i) if (!pop(target_args[i - 1])) return bad("invocation argument stack underflow");
                ObjectRef target_receiver = 0;
                InvokeKind kind = InvokeKind::Static;
                if (op != 0xb8) {
                    if (!pop(x) || !as_ref(x, target_receiver)) return bad("method invocation receiver stack/type error");
                    if (!target_receiver) {
                        if (raise_java_exception("java/lang/NullPointerException", "NullPointerException: method invocation " + cls.this_class + "." + method.name + method.descriptor + " -> " + owner + "." + target_name + target_desc + " pc=" + std::to_string(start), start)) continue;
                        return {false, NullValue{}};
                    }
                    kind = op == 0xb7 ? InvokeKind::Special : InvokeKind::Virtual;
                }
                std::string receiver_class;
                if (target_receiver) {
                    const auto* object = heap_.get(target_receiver);
                    if (!object) return bad("method receiver is not present in Java heap at " + cls.this_class + "." + method.name);
                    if (object->kind != HeapKind::Object) {
                        if (target_name == "clone" && target_desc == "()Ljava/lang/Object;" &&
                            (object->kind == HeapKind::IntArray || object->kind == HeapKind::PrimitiveArray ||
                             object->kind == HeapKind::ReferenceArray)) {
                            ObjectRef copy = 0;
                            if (object->kind == HeapKind::ReferenceArray) {
                                copy = heap_.allocate_reference_array(object->elements.size(), object->component_descriptor);
                            } else {
                                const char descriptor = object->component_descriptor.empty() ? 'I' : object->component_descriptor[0];
                                copy = descriptor == 'I' ? heap_.allocate_int_array(object->elements.size()) :
                                    heap_.allocate_primitive_array(object->elements.size(), descriptor);
                            }
                            if (!copy) return bad("array clone allocation failed");
                            for (std::size_t i = 0; i < object->elements.size(); ++i) {
                                if (!heap_.array_store(copy, i, object->elements[i])) return bad("array clone element copy failed");
                            }
                            if (!push(copy)) return bad("array clone stack overflow");
                            continue;
                        }
                        const std::string receiver_info = "kind=" + std::to_string(static_cast<int>(object->kind)) + ",class=" + object->class_name;
                        return bad("method receiver is not a Java object at " + cls.this_class + "." + method.name + method.descriptor +
                                   " pc=" + std::to_string(start) + " -> " + owner + "." + target_name + target_desc +
                                   " receiver=" + std::to_string(target_receiver) + " (" + receiver_info + ")");
                    }
                    receiver_class = object->class_name;
                }
                // Small deterministic java.lang intrinsics needed by the original
                // Minecraft 1.2.5 entry point. These are VM built-ins, not fake game code.
                if (owner == "java/lang/String" && target_name == "format" && op == 0xb8 &&
                    (target_desc == "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;" ||
                     target_desc == "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;")) {
                    const std::size_t format_index = target_desc[1] == 'L' && target_desc.find("Locale") != std::string::npos ? 1u : 0u;
                    ObjectRef format_ref = 0, values_ref = 0;
                    std::string format;
                    if (!as_ref(target_args[format_index], format_ref) || !heap_.get_string(format_ref, format) ||
                        !as_ref(target_args[format_index + 1], values_ref) || !values_ref)
                        return bad("String.format arguments invalid");
                    const auto* values = heap_.get(values_ref);
                    if (!values || values->kind != HeapKind::ReferenceArray) return bad("String.format arguments must be Object[]");
                    std::string rendered;
                    std::size_t arg_index = 0;
                    for (std::size_t i = 0; i < format.size(); ++i) {
                        if (format[i] != '%' || i + 1 >= format.size()) { rendered.push_back(format[i]); continue; }
                        if (format[i + 1] == '%') { rendered.push_back('%'); ++i; continue; }
                        std::size_t end = i + 1;
                        while (end < format.size() && (std::isdigit(static_cast<unsigned char>(format[end])) || format[end] == '.' || format[end] == '-')) ++end;
                        if (end >= format.size() || arg_index >= values->elements.size()) { rendered.push_back(format[i]); continue; }
                        const char conversion = format[end];
                        const auto& value = values->elements[arg_index++];
                        ObjectRef object_ref = 0;
                        std::string text_value;
                        if (as_ref(value, object_ref) && object_ref) {
                            const auto* object = heap_.get(object_ref);
                            if (object && object->class_name == "java/lang/String") text_value = object->string_value;
                            else if (object && object->class_name == "java/lang/Integer") {
                                JvmValue primitive; if (heap_.get_field(object_ref, "value", primitive) && std::holds_alternative<std::int32_t>(primitive)) text_value = std::to_string(std::get<std::int32_t>(primitive));
                            } else text_value = "null";
                        } else if (std::holds_alternative<std::int32_t>(value)) text_value = std::to_string(std::get<std::int32_t>(value));
                        else if (std::holds_alternative<std::int64_t>(value)) text_value = std::to_string(std::get<std::int64_t>(value));
                        else if (std::holds_alternative<float>(value)) text_value = std::to_string(std::get<float>(value));
                        else if (std::holds_alternative<double>(value)) text_value = std::to_string(std::get<double>(value));
                        if (conversion == 's' || conversion == 'd' || conversion == 'i' || conversion == 'f' || conversion == 'x') rendered += text_value;
                        else { rendered.push_back('%'); rendered.append(format, i + 1, end - i); rendered.push_back(conversion); }
                        i = end;
                    }
                    const ObjectRef result = heap_.allocate_string(rendered);
                    if (!result || !push(result)) return bad("String.format allocation failed");
                    continue;
                }
                if (target_receiver && (owner == "java/lang/String" || receiver_class == "java/lang/String")) {
                    auto* string_object = heap_.get(target_receiver);
                    if (!string_object) return bad("invalid String receiver");
                    auto string_arg = [&](std::size_t index, std::string& value) {
                        ObjectRef ref_value = 0;
                        return index < target_args.size() && as_ref(target_args[index], ref_value) && ref_value && heap_.get_string(ref_value, value);
                    };
                    if (target_name == "equals" && target_desc == "(Ljava/lang/Object;)Z") {
                        std::string other;
                        const bool equal = string_arg(0, other) && other == string_object->string_value;
                        if (!push(std::int32_t(equal ? 1 : 0))) return bad("String.equals stack overflow");
                        continue;
                    }
                    if (target_name == "hashCode" && target_desc == "()I") {
                        std::uint32_t hash = 0;
                        for (unsigned char ch : string_object->string_value) hash = hash * 31u + ch;
                        if (!push(static_cast<std::int32_t>(hash))) return bad("String.hashCode stack overflow");
                        continue;
                    }
                    if (target_name == "toLowerCase" && (target_desc == "()Ljava/lang/String;" ||
                        target_desc == "(Ljava/util/Locale;)Ljava/lang/String;")) {
                        std::string lowered = string_object->string_value;
                        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
                            return static_cast<char>(std::tolower(ch));
                        });
                        const ObjectRef result = heap_.allocate_string(lowered);
                        if (!result || !push(result)) return bad("String.toLowerCase allocation failed");
                        continue;
                    }
                    if (target_name == "toUpperCase" && (target_desc == "()Ljava/lang/String;" ||
                        target_desc == "(Ljava/util/Locale;)Ljava/lang/String;")) {
                        std::string uppered = string_object->string_value;
                        std::transform(uppered.begin(), uppered.end(), uppered.begin(), [](unsigned char ch) {
                            return static_cast<char>(std::toupper(ch));
                        });
                        const ObjectRef result = heap_.allocate_string(uppered);
                        if (!result || !push(result)) return bad("String.toUpperCase allocation failed");
                        continue;
                    }
                    if (target_name == "equalsIgnoreCase" && target_desc == "(Ljava/lang/String;)Z") {
                        std::string other;
                        if (!string_arg(0, other)) return bad("String.equalsIgnoreCase requires String");
                        auto lower = [](std::string value) {
                            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                                return static_cast<char>(std::tolower(ch));
                            });
                            return value;
                        };
                        if (!push(std::int32_t(lower(other) == lower(string_object->string_value) ? 1 : 0)))
                            return bad("String.equalsIgnoreCase stack overflow");
                        continue;
                    }
                    if (target_name == "compareTo" && target_desc == "(Ljava/lang/String;)I") {
                        std::string other;
                        if (!string_arg(0, other)) return bad("String.compareTo requires String");
                        if (!push(static_cast<std::int32_t>(string_object->string_value.compare(other))))
                            return bad("String.compareTo stack overflow");
                        continue;
                    }
                    if (target_name == "toCharArray" && target_desc == "()[C") {
                        const auto& value = string_object->string_value;
                        const ObjectRef chars = heap_.allocate_primitive_array(value.size(), 'C');
                        if (!chars) return bad("String.toCharArray allocation failed");
                        for (std::size_t i = 0; i < value.size(); ++i)
                            if (!heap_.array_store(chars, i, static_cast<std::int32_t>(static_cast<unsigned char>(value[i]))))
                                return bad("String.toCharArray element store failed");
                        if (!push(chars)) return bad("String.toCharArray stack overflow");
                        continue;
                    }
                    if (target_name == "getChars" && target_desc == "(II[CI)V") {
                        const auto begin = std::get<std::int32_t>(target_args[0]);
                        const auto end = std::get<std::int32_t>(target_args[1]);
                        ObjectRef destination = 0;
                        const auto destination_offset = std::get<std::int32_t>(target_args[3]);
                        if (!as_ref(target_args[2], destination) || !destination || begin < 0 || end < begin ||
                            static_cast<std::size_t>(end) > string_object->string_value.size() || destination_offset < 0)
                            return bad("String.getChars bounds/type error");
                        for (int i = begin; i < end; ++i)
                            if (!heap_.array_store(destination, static_cast<std::size_t>(destination_offset + i - begin),
                                static_cast<std::int32_t>(static_cast<unsigned char>(string_object->string_value[static_cast<std::size_t>(i)]))))
                                return bad("String.getChars destination bounds");
                        continue;
                    }
                    if ((target_name == "getBytes" && (target_desc == "()[B" || target_desc == "(Ljava/lang/String;)[B")) ) {
                        const auto& value = string_object->string_value;
                        const ObjectRef bytes = heap_.allocate_primitive_array(value.size(), 'B');
                        if (!bytes) return bad("String.getBytes allocation failed");
                        for (std::size_t i = 0; i < value.size(); ++i)
                            if (!heap_.array_store(bytes, i, static_cast<std::int32_t>(static_cast<unsigned char>(value[i]))))
                                return bad("String.getBytes element store failed");
                        if (!push(bytes)) return bad("String.getBytes stack overflow");
                        continue;
                    }
                    if (target_name == "intern" && target_desc == "()Ljava/lang/String;") {
                        if (!push(target_receiver)) return bad("String.intern stack overflow");
                        continue;
                    }
                    if (target_name == "length" && target_desc == "()I") {
                        if (!push(static_cast<std::int32_t>(string_object->string_value.size()))) return bad("String.length stack overflow");
                        continue;
                    }
                    if (target_name == "isEmpty" && target_desc == "()Z") {
                        if (!push(std::int32_t(string_object->string_value.empty() ? 1 : 0))) return bad("String.isEmpty stack overflow");
                        continue;
                    }
                    if (target_name == "contains" && target_desc == "(Ljava/lang/CharSequence;)Z") {
                        std::string needle;
                        if (!string_arg(0, needle)) return bad("String.contains requires CharSequence backed by String");
                        if (!push(std::int32_t(string_object->string_value.find(needle) != std::string::npos ? 1 : 0)))
                            return bad("String.contains stack overflow");
                        continue;
                    }
                    if (target_name == "startsWith" && target_desc == "(Ljava/lang/String;)Z") {
                        std::string prefix; if (!string_arg(0, prefix)) return bad("String.startsWith requires String");
                        if (!push(std::int32_t(string_object->string_value.rfind(prefix, 0) == 0 ? 1 : 0))) return bad("String.startsWith stack overflow");
                        continue;
                    }
                    if (target_name == "endsWith" && target_desc == "(Ljava/lang/String;)Z") {
                        std::string suffix; if (!string_arg(0, suffix)) return bad("String.endsWith requires String");
                        const auto& value = string_object->string_value;
                        if (!push(std::int32_t(value.size() >= suffix.size() && value.compare(value.size()-suffix.size(), suffix.size(), suffix) == 0 ? 1 : 0))) return bad("String.endsWith stack overflow");
                        continue;
                    }
                    if (target_name == "indexOf" && (target_desc == "(Ljava/lang/String;)I" || target_desc == "(I)I")) {
                        std::size_t found = std::string::npos;
                        if (target_desc == "(Ljava/lang/String;)I") { std::string needle; if (!string_arg(0, needle)) return bad("String.indexOf requires String"); found = string_object->string_value.find(needle); }
                        else found = string_object->string_value.find(static_cast<char>(std::get<std::int32_t>(target_args[0])));
                        if (!push(found == std::string::npos ? std::int32_t(-1) : static_cast<std::int32_t>(found))) return bad("String.indexOf stack overflow");
                        continue;
                    }
                    if (target_name == "charAt" && target_desc == "(I)C") {
                        const auto index = std::get<std::int32_t>(target_args[0]);
                        if (index < 0 || static_cast<std::size_t>(index) >= string_object->string_value.size()) {
                            if (raise_java_exception("java/lang/StringIndexOutOfBoundsException", "String.charAt index out of bounds", start)) continue;
                            return {false, NullValue{}};
                        }
                        if (!push(static_cast<std::int32_t>(static_cast<unsigned char>(string_object->string_value[static_cast<std::size_t>(index)])))) return bad("String.charAt stack overflow");
                        continue;
                    }
                    if (target_name == "substring" && (target_desc == "(I)Ljava/lang/String;" || target_desc == "(II)Ljava/lang/String;")) {
                        const auto begin = std::get<std::int32_t>(target_args[0]);
                        const auto end = target_desc == "(II)Ljava/lang/String;" ? std::get<std::int32_t>(target_args[1]) : static_cast<std::int32_t>(string_object->string_value.size());
                        if (begin < 0 || end < begin || static_cast<std::size_t>(end) > string_object->string_value.size()) {
                            if (raise_java_exception("java/lang/StringIndexOutOfBoundsException", "String.substring index out of bounds", start)) continue;
                            return {false, NullValue{}};
                        }
                        const ObjectRef part = heap_.allocate_string(string_object->string_value.substr(static_cast<std::size_t>(begin), static_cast<std::size_t>(end-begin)));
                        if (!part || !push(part)) return bad("String.substring allocation failed");
                        continue;
                    }
                    if (target_name == "trim" && target_desc == "()Ljava/lang/String;") {
                        const auto& value = string_object->string_value;
                        const auto first = value.find_first_not_of(" \t\r\n\f");
                        const auto last = value.find_last_not_of(" \t\r\n\f");
                        const ObjectRef trimmed = heap_.allocate_string(first == std::string::npos ? std::string() : value.substr(first, last-first+1));
                        if (!trimmed || !push(trimmed)) return bad("String.trim allocation failed");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/lang/String" && target_name == "split" &&
                    (target_desc == "(Ljava/lang/String;)[Ljava/lang/String;" || target_desc == "(Ljava/lang/String;I)[Ljava/lang/String;")) {
                    auto* source = heap_.get(target_receiver);
                    ObjectRef pattern_ref = 0; std::string pattern;
                    if (!source || !as_ref(target_args[0], pattern_ref) || !heap_.get_string(pattern_ref, pattern)) return bad("String.split pattern is not a String");
                    const int limit = target_desc == "(Ljava/lang/String;I)[Ljava/lang/String;" ? std::get<std::int32_t>(target_args[1]) : 0;
                    try {
                        const std::regex expression(pattern);
                        std::sregex_token_iterator it(source->string_value.begin(), source->string_value.end(), expression, -1), end;
                        std::vector<std::string> pieces;
                        for (; it != end; ++it) {
                            if (limit > 0 && pieces.size() + 1 >= static_cast<std::size_t>(limit)) {
                                pieces.push_back(it->str());
                                break;
                            }
                            pieces.push_back(it->str());
                        }
                        if (limit == 0) while (!pieces.empty() && pieces.back().empty()) pieces.pop_back();
                        const ObjectRef result_array = heap_.allocate_reference_array(pieces.size(), "java/lang/String");
                        if (!result_array) return bad("String.split array allocation failed");
                        for (std::size_t i = 0; i < pieces.size(); ++i) {
                            const ObjectRef part = heap_.allocate_string(pieces[i]);
                            if (!part || !heap_.array_store(result_array, i, part)) return bad("String.split element allocation failed");
                        }
                        if (!push(result_array)) return bad("String.split stack overflow");
                        continue;
                    } catch (const std::regex_error&) {
                        if (raise_java_exception("java/util/regex/PatternSyntaxException", "String.split invalid regular expression", start)) continue;
                        return {false, NullValue{}};
                    }
                }
                // Calendar's locale week metadata is only presentation data for
                // this client bootstrap. The bundled JRE 6 implementation obtains
                // it through ResourceBundle.getClassContext(), a VM-native API not
                // exposed by this interpreter; retain Calendar's constructor
                // defaults rather than failing the entire Minecraft startup.
                if (owner == "java/util/Calendar" && target_receiver &&
                    target_name == "setWeekCountData" && target_desc == "(Ljava/util/Locale;)V") continue;
                if (owner == "org/lwjgl/input/Keyboard" && op == 0xb8 && target_name == "isKeyDown" && target_desc == "(I)Z") {
                    const int key = std::get<std::int32_t>(target_args[0]);
                    const bool down = input_host_ && input_host_->key_down && input_host_->key_down(input_host_->user, key);
                    if (!push(std::int32_t(down ? 1 : 0))) return bad("Keyboard.isKeyDown stack overflow");
                    continue;
                }
                if (owner == "org/lwjgl/input/Keyboard" && op == 0xb8) {
                    if (target_name == "poll" && target_desc == "()V") {
                        if (input_host_ && input_host_->poll) input_host_->poll(input_host_->user);
                        continue;
                    }
                    if (target_name == "next" && target_desc == "()Z") {
                        current_keyboard_event_ = {};
                        const bool next = input_host_ && input_host_->next_keyboard_event &&
                            input_host_->next_keyboard_event(input_host_->user, &current_keyboard_event_);
                        if (!push(std::int32_t(next ? 1 : 0))) return bad("Keyboard.next stack overflow");
                        continue;
                    }
                    if (target_name == "getEventKey" && target_desc == "()I") {
                        if (!push(std::int32_t(current_keyboard_event_.key))) return bad("Keyboard.getEventKey stack overflow");
                        continue;
                    }
                    if (target_name == "getEventCharacter" && target_desc == "()C") {
                        if (!push(std::int32_t(current_keyboard_event_.character))) return bad("Keyboard.getEventCharacter stack overflow");
                        continue;
                    }
                    if (target_name == "getEventKeyState" && target_desc == "()Z") {
                        if (!push(std::int32_t(current_keyboard_event_.state ? 1 : 0))) return bad("Keyboard.getEventKeyState stack overflow");
                        continue;
                    }
                    if (target_name == "getEventNanoseconds" && target_desc == "()J") {
                        if (!push(static_cast<std::int64_t>(current_keyboard_event_.nanoseconds))) return bad("Keyboard.getEventNanoseconds stack overflow");
                        continue;
                    }
                }
                if (owner == "org/lwjgl/input/Mouse" && op == 0xb8 && target_name == "isButtonDown" && target_desc == "(I)Z") {
                    const int button = std::get<std::int32_t>(target_args[0]);
                    const bool down = input_host_ && input_host_->mouse_down && input_host_->mouse_down(input_host_->user, button);
                    if (!push(std::int32_t(down ? 1 : 0))) return bad("Mouse.isButtonDown stack overflow");
                    continue;
                }
                if (owner == "org/lwjgl/input/Mouse" && op == 0xb8 && target_name == "poll" && target_desc == "()V") {
                    if (input_host_ && input_host_->poll) input_host_->poll(input_host_->user);
                    continue;
                }
                if (owner == "org/lwjgl/input/Mouse" && op == 0xb8 && target_name == "next" && target_desc == "()Z") {
                    current_mouse_event_ = {};
                    const bool next = input_host_ && input_host_->next_mouse_event && input_host_->next_mouse_event(input_host_->user, &current_mouse_event_);
                    if (!push(std::int32_t(next ? 1 : 0))) return bad("Mouse.next stack overflow");
                    continue;
                }
                if (owner == "org/lwjgl/input/Mouse" && op == 0xb8) {
                    if (target_name == "getEventButton" && target_desc == "()I") {
                        if (!push(std::int32_t(current_mouse_event_.button))) return bad("Mouse.getEventButton stack overflow");
                        continue;
                    }
                    if (target_name == "getEventButtonState" && target_desc == "()Z") {
                        if (!push(std::int32_t(current_mouse_event_.state ? 1 : 0))) return bad("Mouse.getEventButtonState stack overflow");
                        continue;
                    }
                    if (target_name == "getEventX" && target_desc == "()I") {
                        if (!push(std::int32_t(current_mouse_event_.x))) return bad("Mouse.getEventX stack overflow");
                        continue;
                    }
                    if (target_name == "getEventY" && target_desc == "()I") {
                        if (!push(std::int32_t(current_mouse_event_.y))) return bad("Mouse.getEventY stack overflow");
                        continue;
                    }
                    if (target_name == "getEventDWheel" && target_desc == "()I") {
                        if (!push(std::int32_t(current_mouse_event_.wheel))) return bad("Mouse.getEventDWheel stack overflow");
                        continue;
                    }
                    if ((target_name == "getX" || target_name == "getY") && target_desc == "()I") {
                        const int value = input_host_ ? (target_name == "getX"
                            ? (input_host_->mouse_x ? input_host_->mouse_x(input_host_->user) : 0)
                            : (input_host_->mouse_y ? input_host_->mouse_y(input_host_->user) : 0)) : 0;
                        if (!push(std::int32_t(value))) return bad("Mouse position stack overflow");
                        continue;
                    }
                    if ((target_name == "getDX" || target_name == "getDY") && target_desc == "()I") {
                        const int x = input_host_ && input_host_->mouse_x ? input_host_->mouse_x(input_host_->user) : 0;
                        const int y = input_host_ && input_host_->mouse_y ? input_host_->mouse_y(input_host_->user) : 0;
                        const int delta = target_name == "getDX" ? x - previous_mouse_x_ : previous_mouse_y_ - y;
                        previous_mouse_x_ = x;
                        previous_mouse_y_ = y;
                        if (!push(std::int32_t(delta))) return bad("Mouse delta stack overflow");
                        continue;
                    }
                    if (target_name == "setGrabbed" && target_desc == "(Z)V") {
                        mouse_grabbed_ = std::get<std::int32_t>(target_args[0]) != 0;
                        continue;
                    }
                    if (target_name == "isGrabbed" && target_desc == "()Z") {
                        if (!push(std::int32_t(mouse_grabbed_ ? 1 : 0))) return bad("Mouse.isGrabbed stack overflow");
                        continue;
                    }
                    if (target_name == "setCursorPosition" && target_desc == "(II)V") continue;
                }                if (owner == "java/lang/Thread" && op == 0xb8 && target_name == "yield" && target_desc == "()V") continue;
                if (owner == "java/lang/Thread" && op == 0xb8 && target_name == "sleep" && target_desc == "(J)V") continue;
                if (owner == "java/util/Locale" && target_name == "getDefault" &&
                    target_desc == "()Ljava/util/Locale;" && op == 0xb8) {
                    // The bundled JRE is Java 6: Locale stores language/country/
                    // variant directly (not through modern BaseLocale). Supply
                    // a deterministic en_US default for the host probe until
                    // system-property-backed locale construction is wired in.
                    const ObjectRef locale = heap_.allocate_object("java/util/Locale");
                    const ObjectRef language = heap_.allocate_string("en");
                    const ObjectRef country = heap_.allocate_string("US");
                    const ObjectRef variant = heap_.allocate_string("");
                    if (!locale || !language || !country || !variant ||
                        !heap_.put_field(locale, "language", language) ||
                        !heap_.put_field(locale, "country", country) ||
                        !heap_.put_field(locale, "variant", variant) || !push(locale))
                        return bad("Locale.getDefault allocation failed");
                    continue;
                }
                if (owner == "java/text/NumberFormat" && op == 0xb8 &&
                    (target_name == "getIntegerInstance" || target_name == "getInstance" || target_name == "getNumberInstance") &&
                    (target_desc == "()Ljava/text/NumberFormat;" || target_desc == "(Ljava/util/Locale;)Ljava/text/NumberFormat;")) {
                    const ObjectRef format = heap_.allocate_object("java/text/NumberFormat");
                    if (!format || !push(format)) return bad("NumberFormat factory allocation failed");
                    continue;
                }
                if (target_receiver && owner == "java/text/DecimalFormat" && target_name == "<init>" &&
                    (target_desc == "(Ljava/lang/String;)V" || target_desc == "(Ljava/lang/String;Ljava/text/DecimalFormatSymbols;)V")) continue;
                if (target_receiver && (owner == "java/text/NumberFormat" || owner == "java/text/DecimalFormat") && target_name == "format") {
                    std::string formatted;
                    if (target_desc == "(I)Ljava/lang/String;" && std::holds_alternative<std::int32_t>(target_args[0]))
                        formatted = std::to_string(std::get<std::int32_t>(target_args[0]));
                    else if (target_desc == "(J)Ljava/lang/String;" && std::holds_alternative<std::int64_t>(target_args[0]))
                        formatted = std::to_string(std::get<std::int64_t>(target_args[0]));
                    else return bad("unsupported NumberFormat.format descriptor");
                    const ObjectRef text_ref = heap_.allocate_string(formatted);
                    if (!text_ref || !push(text_ref)) return bad("NumberFormat.format allocation failed");
                    continue;
                }
                // Boxed primitive intrinsics. Wrapper objects keep the primitive
                // payload in a typed VM field so collections can retain them.
                if (owner == "java/lang/Character" && op == 0xb8 && target_name == "valueOf" && target_desc == "(C)Ljava/lang/Character;") {
                    const ObjectRef boxed = heap_.allocate_object("java/lang/Character");
                    if (!boxed || !heap_.put_field(boxed, "value", target_args[0]) || !push(boxed)) return bad("Character.valueOf allocation failed");
                    continue;
                }
                if (target_receiver && owner == "java/lang/Character") {
                    JvmValue primitive; if (!heap_.get_field(target_receiver, "value", primitive)) primitive = std::int32_t(0);
                    if (target_name == "charValue" && target_desc == "()C") { if (!push(primitive)) return bad("Character.charValue stack overflow"); continue; }
                    if (target_name == "hashCode" && target_desc == "()I") { if (!push(primitive)) return bad("Character.hashCode stack overflow"); continue; }
                    if (target_name == "equals" && target_desc == "(Ljava/lang/Object;)Z") {
                        ObjectRef other=0; as_ref(target_args[0],other); JvmValue other_value;
                        const bool found=other&&heap_.get_field(other,"value",other_value);
                        const auto* left=std::get_if<std::int32_t>(&primitive); const auto* right=std::get_if<std::int32_t>(&other_value);
                        if (!push(std::int32_t(found&&left&&right&&*left==*right?1:0))) return bad("Character.equals stack overflow");
                        continue;
                    }
                }
                if (owner == "java/lang/Float" && op == 0xb8 && target_name == "valueOf" && target_desc == "(F)Ljava/lang/Float;") {
                    const ObjectRef boxed = heap_.allocate_object("java/lang/Float");
                    if (!boxed || !heap_.put_field(boxed, "value", target_args[0]) || !push(boxed)) return bad("Float.valueOf(float) allocation failed");
                    continue;
                }
                if (owner == "java/lang/Double" && op == 0xb8 && target_name == "valueOf" && target_desc == "(D)Ljava/lang/Double;") {
                    const ObjectRef boxed = heap_.allocate_object("java/lang/Double");
                    if (!boxed || !heap_.put_field(boxed, "value", target_args[0]) || !push(boxed)) return bad("Double.valueOf(double) allocation failed");
                    continue;
                }
                if (owner == "java/lang/Boolean" && op == 0xb8 &&
                    (target_name == "valueOf" || target_name == "parseBoolean") &&
                    (target_desc == "(Ljava/lang/String;)Ljava/lang/Boolean;" || target_desc == "(Ljava/lang/String;)Z" ||
                     target_desc == "(Z)Ljava/lang/Boolean;")) {
                    bool value = false;
                    if (target_desc == "(Z)Ljava/lang/Boolean;") value = std::get<std::int32_t>(target_args[0]) != 0;
                    else {
                        ObjectRef text_ref = 0; std::string text;
                        if (as_ref(target_args[0], text_ref) && text_ref) heap_.get_string(text_ref, text);
                        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                        value = text == "true";
                    }
                    if (target_desc == "(Ljava/lang/String;)Z") {
                        if (!push(std::int32_t(value ? 1 : 0))) return bad("Boolean.parseBoolean stack overflow");
                    } else {
                        const ObjectRef boxed = heap_.allocate_object("java/lang/Boolean");
                        if (!boxed || !heap_.put_field(boxed, "value", std::int32_t(value ? 1 : 0)) || !push(boxed))
                            return bad("Boolean.valueOf allocation failed");
                    }
                    continue;
                }
                if (owner == "java/lang/Integer" && op == 0xb8 && target_desc == "(I)Ljava/lang/String;" &&
                    (target_name == "toHexString" || target_name == "toBinaryString" || target_name == "toOctalString" || target_name == "toString")) {
                    const std::uint32_t value = static_cast<std::uint32_t>(std::get<std::int32_t>(target_args[0]));
                    std::ostringstream formatted;
                    const int radix = target_name == "toHexString" ? 16 : (target_name == "toBinaryString" ? 2 : (target_name == "toOctalString" ? 8 : 10));
                    if (radix == 10) formatted << static_cast<std::int32_t>(value);
                    else formatted << std::setbase(radix) << value;
                    const ObjectRef result = heap_.allocate_string(formatted.str());
                    if (!result || !push(result)) return bad("Integer radix conversion allocation failed");
                    continue;
                }
                if (owner == "java/lang/Integer" && op == 0xb8 &&
                    (target_name == "parseInt" || target_name == "valueOf") &&
                    (target_desc == "(Ljava/lang/String;)I" || target_desc == "(Ljava/lang/String;I)I" ||
                     target_desc == "(Ljava/lang/String;)Ljava/lang/Integer;")) {
                    ObjectRef text_ref = 0; std::string text;
                    if (!as_ref(target_args[0], text_ref) || !heap_.get_string(text_ref, text)) return bad("Integer.parseInt requires String");
                    const int radix = target_desc == "(Ljava/lang/String;I)I" ? std::get<std::int32_t>(target_args[1]) : 10;
                    try {
                        std::size_t used = 0;
                        const long parsed = std::stol(text, &used, radix);
                        if (used != text.size() || parsed < -2147483648L || parsed > 2147483647L || radix < 2 || radix > 36)
                            throw std::out_of_range("integer");
                        const auto value = static_cast<std::int32_t>(parsed);
                        if (target_desc == "(Ljava/lang/String;)Ljava/lang/Integer;") {
                            const ObjectRef boxed = heap_.allocate_object("java/lang/Integer");
                            if (!boxed || !heap_.put_field(boxed, "value", value) || !push(boxed)) return bad("Integer.valueOf allocation failed");
                        } else if (!push(value)) return bad("Integer.parseInt stack overflow");
                        continue;
                    } catch (const std::exception&) {
                        if (raise_java_exception("java/lang/NumberFormatException", "invalid integer text", start)) continue;
                        return {false, NullValue{}};
                    }
                }
                if (owner == "java/lang/Integer" && target_name == "valueOf" &&
                    target_desc == "(I)Ljava/lang/Integer;" && op == 0xb8) {
                    const ObjectRef boxed = heap_.allocate_object("java/lang/Integer");
                    if (!boxed || !heap_.put_field(boxed, "value", target_args[0]) || !push(boxed))
                        return bad("Integer.valueOf allocation failed");
                    continue;
                }
                if (target_receiver && (owner == "java/lang/Float" || owner == "java/lang/Double")) {
                    JvmValue primitive;
                    const bool have_value = heap_.get_field(target_receiver, "value", primitive);
                    if (!have_value) primitive = owner == "java/lang/Float" ? JvmValue(0.0f) : JvmValue(0.0);
                    double numeric = 0.0;
                    if (const auto* value = std::get_if<float>(&primitive)) numeric = *value;
                    else if (const auto* value = std::get_if<double>(&primitive)) numeric = *value;
                    else if (const auto* value = std::get_if<std::int32_t>(&primitive)) numeric = *value;
                    if (target_name == "floatValue" && target_desc == "()F") {
                        if (!push(static_cast<float>(numeric))) return bad("Float.floatValue stack overflow");
                        continue;
                    }
                    if (target_name == "doubleValue" && target_desc == "()D") {
                        if (!push(numeric)) return bad("Double.doubleValue stack overflow");
                        continue;
                    }
                    if (target_name == "intValue" && target_desc == "()I") {
                        if (!push(static_cast<std::int32_t>(numeric))) return bad("Number.intValue stack overflow");
                        continue;
                    }
                    if (target_name == "longValue" && target_desc == "()J") {
                        if (!push(static_cast<std::int64_t>(numeric))) return bad("Number.longValue stack overflow");
                        continue;
                    }
                    if (target_name == "toString" && target_desc == "()Ljava/lang/String;") {
                        const ObjectRef text_ref = heap_.allocate_string(owner == "java/lang/Float" ?
                            std::to_string(static_cast<float>(numeric)) : std::to_string(numeric));
                        if (!text_ref || !push(text_ref)) return bad("floating wrapper toString allocation failed");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/util/concurrent/atomic/AtomicInteger") {
                    JvmValue current_value;
                    if (!heap_.get_field(target_receiver, "value", current_value) || !std::holds_alternative<std::int32_t>(current_value)) current_value = std::int32_t(0);
                    const auto current = std::get<std::int32_t>(current_value);
                    if (target_name == "get" && target_desc == "()I") { if (!push(current)) return bad("AtomicInteger.get stack overflow"); continue; }
                    if (target_name == "set" && target_desc == "(I)V") { if (!heap_.put_field(target_receiver, "value", target_args[0])) return bad("AtomicInteger.set failed"); continue; }
                    if (target_name == "lazySet" && target_desc == "(I)V") { if (!heap_.put_field(target_receiver, "value", target_args[0])) return bad("AtomicInteger.lazySet failed"); continue; }
                    if (target_name == "getAndAdd" && target_desc == "(I)I") {
                        const auto next = static_cast<std::int32_t>(current + std::get<std::int32_t>(target_args[0]));
                        if (!heap_.put_field(target_receiver, "value", next) || !push(current)) return bad("AtomicInteger.getAndAdd failed");
                        continue;
                    }
                    if (target_name == "addAndGet" && target_desc == "(I)I") {
                        const auto next = static_cast<std::int32_t>(current + std::get<std::int32_t>(target_args[0]));
                        if (!heap_.put_field(target_receiver, "value", next) || !push(next)) return bad("AtomicInteger.addAndGet failed");
                        continue;
                    }
                    if (target_name == "getAndIncrement" && target_desc == "()I") {
                        if (!heap_.put_field(target_receiver, "value", static_cast<std::int32_t>(current + 1)) || !push(current)) return bad("AtomicInteger.getAndIncrement failed");
                        continue;
                    }
                    if (target_name == "incrementAndGet" && target_desc == "()I") {
                        const auto next = static_cast<std::int32_t>(current + 1);
                        if (!heap_.put_field(target_receiver, "value", next) || !push(next)) return bad("AtomicInteger.incrementAndGet failed");
                        continue;
                    }
                    if (target_name == "compareAndSet" && target_desc == "(II)Z") {
                        const bool match = current == std::get<std::int32_t>(target_args[0]);
                        if (match && !heap_.put_field(target_receiver, "value", target_args[1])) return bad("AtomicInteger.compareAndSet failed");
                        if (!push(std::int32_t(match ? 1 : 0))) return bad("AtomicInteger.compareAndSet stack overflow");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/lang/Boolean") {
                    JvmValue primitive = std::int32_t(0);
                    heap_.get_field(target_receiver, "value", primitive);
                    if (target_name == "booleanValue" && target_desc == "()Z") {
                        if (!push(primitive)) return bad("Boolean.booleanValue stack overflow");
                        continue;
                    }
                    if (target_name == "toString" && target_desc == "()Ljava/lang/String;") {
                        const bool value = std::holds_alternative<std::int32_t>(primitive) && std::get<std::int32_t>(primitive) != 0;
                        const ObjectRef result = heap_.allocate_string(value ? "true" : "false");
                        if (!result || !push(result)) return bad("Boolean.toString allocation failed");
                        continue;
                    }
                    if (target_name == "equals" && target_desc == "(Ljava/lang/Object;)Z") {
                        ObjectRef other = 0; as_ref(target_args[0], other);
                        JvmValue other_value;
                        const bool same = other && heap_.get_field(other, "value", other_value) &&
                            std::holds_alternative<std::int32_t>(primitive) && std::holds_alternative<std::int32_t>(other_value) &&
                            std::get<std::int32_t>(primitive) == std::get<std::int32_t>(other_value);
                        if (!push(std::int32_t(same ? 1 : 0))) return bad("Boolean.equals stack overflow");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/lang/Integer") {
                    if (target_name == "<init>" && target_desc == "(I)V") {
                        if (!heap_.put_field(target_receiver,"value",target_args[0])) return bad("Integer constructor field setup failed");
                        continue;
                    }
                    if (target_name == "<init>" && target_desc == "(Ljava/lang/String;)V") {
                        ObjectRef text_ref=0; std::string text; if(!as_ref(target_args[0],text_ref)||!heap_.get_string(text_ref,text))return bad("Integer(String) requires String");
                        try { heap_.put_field(target_receiver,"value",static_cast<std::int32_t>(std::stol(text))); } catch(...) { return bad("NumberFormatException: invalid Integer string"); }
                        continue;
                    }
                    JvmValue primitive;
                    if (!heap_.get_field(target_receiver, "value", primitive)) primitive = std::int32_t(0);
                    if (target_name == "intValue" && target_desc == "()I") {
                        if (!push(primitive)) return bad("Integer.intValue stack overflow");
                        continue;
                    }
                    if (target_name == "hashCode" && target_desc == "()I") {
                        if (!push(primitive)) return bad("Integer.hashCode stack overflow");
                        continue;
                    }
                    if (target_name == "equals" && target_desc == "(Ljava/lang/Object;)Z") {
                        ObjectRef other = 0; as_ref(target_args[0], other);
                        JvmValue other_value;
                        const auto* left = std::get_if<std::int32_t>(&primitive);
                        const bool found = other && heap_.get_field(other, "value", other_value);
                        const auto* right = std::get_if<std::int32_t>(&other_value);
                        const bool equal = found && left && right && *left == *right;
                        if (!push(std::int32_t(equal ? 1 : 0))) return bad("Integer.equals stack overflow");
                        continue;
                    }
                }
                if (owner == "java/lang/System" && target_name == "getProperty" && op == 0xb8 &&
                    (target_desc == "(Ljava/lang/String;)Ljava/lang/String;" ||
                     target_desc == "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;")) {
                    ObjectRef key_ref = 0;
                    std::string key;
                    if (!as_ref(target_args[0], key_ref) || !key_ref || !heap_.get_string(key_ref, key))
                        return bad("System.getProperty key must be a String");
                    JvmValue overridden;
                    if (statics_.get("opti/switchport/SystemProperties", key, overridden)) {
                        if (!push(overridden)) return bad("System.getProperty override stack overflow");
                        continue;
                    }
                    std::string value;
                    bool known = true;
                    if (key == "os.name") value = "Linux";
                    else if (key == "os.arch") value = "aarch64";
                    else if (key == "java.version") value = "1.8.0";
                    else if (key == "java.specification.version") value = "1.8";
                    else if (key == "java.vendor") value = "OpenJDK";
                    else if (key == "java.awt.headless") value = "false";
                    else if (key == "file.encoding") value = "UTF-8";
                    else if (key == "file.separator") value = "/";
                    else if (key == "path.separator") value = ":";
                    else if (key == "line.separator") value = "\n";
                    else if (key == "user.language") value = "en";
                    else if (key == "user.country") value = "US";
                    else if (key == "user.home") value = "/switch/opticraft";
                    else if (key == "user.dir") value = "/switch/opticraft";
                    else if (key == "sun.arch.data.model") value = "64";
                    else if (key == "java.class.version") value = "52.0";
                    else known = false;
                    JvmValue result = NullValue{};
                    if (known) {
                        const ObjectRef value_ref = heap_.allocate_string(value);
                        if (!value_ref) return bad("System.getProperty value allocation failed");
                        result = value_ref;
                    } else if (target_args.size() == 2) {
                        result = target_args[1];
                    }
                    if (!push(result)) return bad("System.getProperty stack overflow");
                    continue;
                }
                if (owner == "java/lang/System" && target_name == "setProperty" && op == 0xb8 &&
                    target_desc == "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;") {
                    ObjectRef key_ref = 0;
                    if (!as_ref(target_args[0], key_ref) || !key_ref) return bad("System.setProperty key is null");
                    std::string key;
                    if (!heap_.get_string(key_ref, key)) return bad("System.setProperty key is invalid");
                    JvmValue previous = NullValue{};
                    statics_.get("opti/switchport/SystemProperties", key, previous);
                    statics_.set("opti/switchport/SystemProperties", key, target_args[1]);
                    if (!push(previous)) return bad("System.setProperty stack overflow");
                    continue;
                }
                if (owner == "java/lang/System" && target_name == "currentTimeMillis" && target_desc == "()J" && op == 0xb8) {
                    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    if (!push(static_cast<std::int64_t>(now))) return bad("operand stack overflow");
                    continue;
                }
                if (owner == "java/lang/System" && target_name == "nanoTime" && target_desc == "()J" && op == 0xb8) {
                    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    if (!push(static_cast<std::int64_t>(now))) return bad("operand stack overflow");
                    continue;
                }
                if (owner == "java/lang/System" && target_name == "gc" && target_desc == "()V" && op == 0xb8) continue;
                if ((owner == "java/lang/Float" || owner == "java/lang/Double") && op == 0xb8 &&
                    (target_name == "parseFloat" || target_name == "valueOf" || target_name == "parseDouble") &&
                    (target_desc == "(Ljava/lang/String;)F" || target_desc == "(Ljava/lang/String;)D" ||
                     target_desc == "(Ljava/lang/String;)Ljava/lang/Float;" || target_desc == "(Ljava/lang/String;)Ljava/lang/Double;")) {
                    ObjectRef text_ref = 0;
                    std::string text_value;
                    if (!as_ref(target_args[0], text_ref) || !heap_.get_string(text_ref, text_value))
                        return bad(owner + "." + target_name + " requires String");
                    try {
                        std::size_t consumed = 0;
                        const bool as_double = target_desc == "(Ljava/lang/String;)D" ||
                                               target_desc == "(Ljava/lang/String;)Ljava/lang/Double;";
                        if (as_double) {
                            const double value = std::stod(text_value, &consumed);
                            if (consumed != text_value.size()) throw std::invalid_argument("trailing data");
                            if (target_desc == "(Ljava/lang/String;)Ljava/lang/Double;") {
                                const ObjectRef boxed = heap_.allocate_object("java/lang/Double");
                                if (!boxed || !heap_.put_field(boxed, "value", value) || !push(boxed)) return bad("Double.valueOf allocation failed");
                            } else if (!push(value)) return bad("Double.parseDouble stack overflow");
                        } else {
                            const float value = std::stof(text_value, &consumed);
                            if (consumed != text_value.size()) throw std::invalid_argument("trailing data");
                            if (target_desc == "(Ljava/lang/String;)Ljava/lang/Float;") {
                                const ObjectRef boxed = heap_.allocate_object("java/lang/Float");
                                if (!boxed || !heap_.put_field(boxed, "value", value) || !push(boxed)) return bad("Float.valueOf allocation failed");
                            } else if (!push(value)) return bad("Float.parseFloat stack overflow");
                        }
                        continue;
                    } catch (const std::exception&) {
                        if (raise_java_exception("java/lang/NumberFormatException", "invalid floating point text", start)) continue;
                        return {false, NullValue{}};
                    }
                }
                if (owner == "java/lang/StringBuilder" && target_receiver != 0) {
                    auto* builder = heap_.get(target_receiver);
                    if (!builder || builder->class_name != "java/lang/StringBuilder") return bad("invalid StringBuilder receiver");
                    if (target_name == "<init>" && target_desc == "()V") continue;
                    if (target_name == "append" && target_desc == "(Ljava/lang/String;)Ljava/lang/StringBuilder;" && target_args.size() == 1) {
                        ObjectRef string_ref = 0; std::string text;
                        if (!as_ref(target_args[0], string_ref) || (string_ref && !heap_.get_string(string_ref, text))) return bad("StringBuilder.append(String) received invalid string");
                        builder->string_value += text;
                        if (!push(target_receiver)) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "append" && (target_desc == "(I)Ljava/lang/StringBuilder;" || target_desc == "(Z)Ljava/lang/StringBuilder;" || target_desc == "(C)Ljava/lang/StringBuilder;") && target_args.size() == 1 && std::holds_alternative<std::int32_t>(target_args[0])) {
                        const auto value = std::get<std::int32_t>(target_args[0]);
                        if (target_desc == "(Z)Ljava/lang/StringBuilder;") builder->string_value += value ? "true" : "false";
                        else if (target_desc == "(C)Ljava/lang/StringBuilder;") builder->string_value.push_back(static_cast<char>(value));
                        else builder->string_value += std::to_string(value);
                        if (!push(target_receiver)) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "append" && target_desc == "(F)Ljava/lang/StringBuilder;" && std::holds_alternative<float>(target_args[0])) {
                        builder->string_value += std::to_string(std::get<float>(target_args[0]));
                        if (!push(target_receiver)) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "append" && target_desc == "(D)Ljava/lang/StringBuilder;" && std::holds_alternative<double>(target_args[0])) {
                        builder->string_value += std::to_string(std::get<double>(target_args[0]));
                        if (!push(target_receiver)) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "append" && target_desc == "(Ljava/lang/Object;)Ljava/lang/StringBuilder;") {
                        ObjectRef object_ref = 0; std::string text;
                        if (as_ref(target_args[0], object_ref) && object_ref && heap_.get_string(object_ref, text)) builder->string_value += text;
                        else if (std::holds_alternative<std::int32_t>(target_args[0])) builder->string_value += std::to_string(std::get<std::int32_t>(target_args[0]));
                        else builder->string_value += "null";
                        if (!push(target_receiver)) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "append" && target_desc == "(J)Ljava/lang/StringBuilder;" && target_args.size() == 1 && std::holds_alternative<std::int64_t>(target_args[0])) {
                        builder->string_value += std::to_string(std::get<std::int64_t>(target_args[0]));
                        if (!push(target_receiver)) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "toString" && target_desc == "()Ljava/lang/String;") {
                        const ObjectRef string_ref = heap_.allocate_string(builder->string_value);
                        if (!string_ref || !push(string_ref)) return bad("StringBuilder.toString allocation failed");
                        continue;
                    }
                }
                if (owner == "java/util/logging/Logger" && op == 0xb8 &&
                    ((target_name == "getAnonymousLogger" && target_desc == "()Ljava/util/logging/Logger;") ||
                     (target_name == "getLogger" && target_desc == "(Ljava/lang/String;)Ljava/util/logging/Logger;"))) {
                    const ObjectRef logger=heap_.allocate_object("java/util/logging/Logger");
                    if(!logger)return bad("Logger allocation failed");
                    if(target_name=="getLogger"){ObjectRef name=0;std::string text;if(as_ref(target_args[0],name))heap_.get_string(name,text);heap_.put_field(logger,"logger_name",heap_.allocate_string(text));}
                    if(!push(logger))return bad("Logger factory stack overflow");
                    continue;
                }
                if (target_receiver && owner == "java/util/logging/Logger") {
                    if(target_name=="getName"&&target_desc=="()Ljava/lang/String;"){JvmValue name; if(!heap_.get_field(target_receiver,"logger_name",name))name=heap_.allocate_string("");if(!push(name))return bad("Logger.getName stack overflow");continue;}
                    if(target_name=="setLevel"&&target_desc=="(Ljava/util/logging/Level;)V")continue;
                    if(target_name=="getLevel"&&target_desc=="()Ljava/util/logging/Level;"){if(!push(NullValue{}))return bad("Logger.getLevel stack overflow");continue;}
                    if(target_name=="info"||target_name=="warning"||target_name=="fine"||target_name=="finer"||target_name=="finest"||target_name=="severe"||target_name=="config"||target_name=="log"||target_name=="entering"||target_name=="exiting")continue;
                }
                if (owner == "java/lang/Float" && op == 0xb8) {
                    if ((target_name == "floatToRawIntBits" || target_name == "floatToIntBits") && target_desc == "(F)I") {
                        const float value=std::get<float>(target_args[0]);std::uint32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));if(target_name=="floatToIntBits"&&std::isnan(value))bits=0x7fc00000u;
                        if(!push(static_cast<std::int32_t>(bits)))return bad("Float.floatToIntBits stack overflow");
                        continue;
                    }
                    if (target_name == "intBitsToFloat" && target_desc == "(I)F") { const auto bits=static_cast<std::uint32_t>(std::get<std::int32_t>(target_args[0]));float value=0;std::memcpy(&value,&bits,sizeof(value));if(!push(value))return bad("Float.intBitsToFloat stack overflow");continue; }
                }
                if ((owner == "java/lang/Math" || owner == "java/lang/StrictMath") && op == 0xb8) {
                    if (target_name == "ceil" && target_desc == "(D)D" && std::holds_alternative<double>(target_args[0])) { if(!push(std::ceil(std::get<double>(target_args[0]))))return bad("Math.ceil stack overflow"); continue; }
                    if (target_name == "floor" && target_desc == "(D)D" && std::holds_alternative<double>(target_args[0])) { if(!push(std::floor(std::get<double>(target_args[0]))))return bad("Math.floor stack overflow"); continue; }
                    if (target_name == "sqrt" && target_desc == "(D)D" && std::holds_alternative<double>(target_args[0])) { if(!push(std::sqrt(std::get<double>(target_args[0]))))return bad("Math.sqrt stack overflow"); continue; }
                    if (target_name == "sin" && target_desc == "(D)D" && std::holds_alternative<double>(target_args[0])) { if(!push(std::sin(std::get<double>(target_args[0]))))return bad("Math.sin stack overflow"); continue; }
                    if (target_name == "cos" && target_desc == "(D)D" && std::holds_alternative<double>(target_args[0])) { if(!push(std::cos(std::get<double>(target_args[0]))))return bad("Math.cos stack overflow"); continue; }
                    if (target_name == "tan" && target_desc == "(D)D" && std::holds_alternative<double>(target_args[0])) { if(!push(std::tan(std::get<double>(target_args[0]))))return bad("Math.tan stack overflow"); continue; }
                    if (target_name == "atan2" && target_desc == "(DD)D") { if(!push(std::atan2(std::get<double>(target_args[0]),std::get<double>(target_args[1]))))return bad("Math.atan2 stack overflow"); continue; }
                    if (target_name == "atan" && target_desc == "(D)D") { if(!push(std::atan(std::get<double>(target_args[0]))))return bad("Math.atan stack overflow"); continue; }
                    if (target_name == "asin" && target_desc == "(D)D") { if(!push(std::asin(std::get<double>(target_args[0]))))return bad("Math.asin stack overflow"); continue; }
                    if (target_name == "acos" && target_desc == "(D)D") { if(!push(std::acos(std::get<double>(target_args[0]))))return bad("Math.acos stack overflow"); continue; }
                    if (target_name == "pow" && target_desc == "(DD)D") { if(!push(std::pow(std::get<double>(target_args[0]),std::get<double>(target_args[1]))))return bad("Math.pow stack overflow"); continue; }
                    if (target_name == "exp" && target_desc == "(D)D") { if(!push(std::exp(std::get<double>(target_args[0]))))return bad("Math.exp stack overflow"); continue; }
                    if (target_name == "log" && target_desc == "(D)D") { if(!push(std::log(std::get<double>(target_args[0]))))return bad("Math.log stack overflow"); continue; }
                    if (target_name == "log10" && target_desc == "(D)D") { if(!push(std::log10(std::get<double>(target_args[0]))))return bad("Math.log10 stack overflow"); continue; }
                    if (target_name == "round" && target_desc == "(F)I") { if(!push(static_cast<std::int32_t>(std::llround(std::get<float>(target_args[0])))))return bad("Math.round stack overflow"); continue; }
                    if (target_name == "round" && target_desc == "(D)J") { if(!push(static_cast<std::int64_t>(std::llround(std::get<double>(target_args[0])))))return bad("Math.round stack overflow"); continue; }
                    if (target_name == "max" && target_desc == "(DD)D") { if(!push(std::max(std::get<double>(target_args[0]),std::get<double>(target_args[1]))))return bad("Math.max stack overflow"); continue; }
                    if (target_name == "min" && target_desc == "(DD)D") { if(!push(std::min(std::get<double>(target_args[0]),std::get<double>(target_args[1]))))return bad("Math.min stack overflow"); continue; }
                    if (target_name == "toRadians" && target_desc == "(D)D" && std::holds_alternative<double>(target_args[0])) { if(!push(std::get<double>(target_args[0]) * 0.017453292519943295769))return bad("Math.toRadians stack overflow"); continue; }
                    if (target_name == "toDegrees" && target_desc == "(D)D" && std::holds_alternative<double>(target_args[0])) { if(!push(std::get<double>(target_args[0]) * 57.2957795130823208768))return bad("Math.toDegrees stack overflow"); continue; }
                    if (target_name == "abs" && target_desc == "(I)I") { if(!push(std::int32_t(std::abs(std::get<std::int32_t>(target_args[0])))))return bad("Math.abs stack overflow"); continue; }
                    if (target_name == "abs" && target_desc == "(D)D") { if(!push(std::abs(std::get<double>(target_args[0]))))return bad("Math.abs stack overflow"); continue; }
                    if (target_name == "max" && target_desc == "(II)I") { if(!push(std::max(std::get<std::int32_t>(target_args[0]),std::get<std::int32_t>(target_args[1]))))return bad("Math.max stack overflow"); continue; }
                    if (target_name == "min" && target_desc == "(II)I") { if(!push(std::min(std::get<std::int32_t>(target_args[0]),std::get<std::int32_t>(target_args[1]))))return bad("Math.min stack overflow"); continue; }
                }
                if (owner == "java/lang/Double" && op == 0xb8) {
                    if ((target_name == "doubleToRawLongBits" || target_name == "doubleToLongBits") && target_desc == "(D)J") {
                        const double value = std::get<double>(target_args[0]);
                        std::int64_t bits = 0;
                        static_assert(sizeof(bits) == sizeof(value), "double must be 64-bit");
                        std::memcpy(&bits, &value, sizeof(bits));
                        if (!push(bits)) return bad("Double.doubleToLongBits stack overflow");
                        continue;
                    }
                    if (target_name == "longBitsToDouble" && target_desc == "(J)D") {
                        const std::int64_t bits = std::get<std::int64_t>(target_args[0]);
                        double value = 0.0;
                        std::memcpy(&value, &bits, sizeof(value));
                        if (!push(value)) return bad("Double.longBitsToDouble stack overflow");
                        continue;
                    }
                }
                if (owner == "java/lang/Float" && op == 0xb8) {
                    if ((target_name == "floatToRawIntBits" || target_name == "floatToIntBits") && target_desc == "(F)I") {
                        const float value = std::get<float>(target_args[0]);
                        std::int32_t bits = 0;
                        static_assert(sizeof(bits) == sizeof(value), "float must be 32-bit");
                        std::memcpy(&bits, &value, sizeof(bits));
                        if (!push(bits)) return bad("Float.floatToIntBits stack overflow");
                        continue;
                    }
                    if (target_name == "intBitsToFloat" && target_desc == "(I)F") {
                        const std::int32_t bits = std::get<std::int32_t>(target_args[0]);
                        float value = 0.0f;
                        std::memcpy(&value, &bits, sizeof(value));
                        if (!push(value)) return bad("Float.intBitsToFloat stack overflow");
                        continue;
                    }
                }
                if (owner == "java/nio/ByteOrder" && op == 0xb8 && target_name == "nativeOrder" && target_desc == "()Ljava/nio/ByteOrder;") {
                    JvmValue cached; ObjectRef order = 0;
                    if (statics_.get("opti/switchport/ByteOrder", "native", cached)) as_ref(cached, order);
                    if (!order) { order = heap_.allocate_object("java/nio/ByteOrder"); if (!order || !statics_.define("opti/switchport/ByteOrder", "native", order)) return bad("ByteOrder allocation failed"); }
                    if (!push(order)) return bad("ByteOrder.nativeOrder stack overflow");
                    continue;
                }
                if (owner == "java/nio/ByteBuffer" && op == 0xb8 &&
                    (target_name == "allocateDirect" || target_name == "allocate") &&
                    target_desc == "(I)Ljava/nio/ByteBuffer;") {
                    const auto capacity = std::get<std::int32_t>(target_args[0]);
                    if (capacity < 0 || capacity > 64 * 1024 * 1024) return bad("ByteBuffer.allocateDirect invalid capacity");
                    const ObjectRef buffer = heap_.allocate_object("java/nio/ByteBuffer");
                    auto* object = heap_.get(buffer);
                    if (!buffer || !object) return bad("ByteBuffer allocation failed");
                    object->elements.resize(static_cast<std::size_t>(capacity), std::int32_t(0));
                    object->fields["buffer_capacity"] = capacity;
                    object->fields["buffer_limit"] = capacity;
                    object->fields["buffer_position"] = std::int32_t(0);
                    object->fields["buffer_direct"] = std::int32_t(target_name == "allocateDirect" ? 1 : 0);
                    if (!push(buffer)) return bad("ByteBuffer.allocateDirect stack overflow");
                    continue;
                }
                if (target_receiver && owner == "java/nio/ByteBuffer" && target_name.rfind("as",0)==0 &&
                    (target_desc == "()Ljava/nio/IntBuffer;" || target_desc == "()Ljava/nio/FloatBuffer;" || target_desc == "()Ljava/nio/ShortBuffer;")) {
                    const auto* parent = heap_.get(target_receiver);
                    if (!parent) return bad("invalid ByteBuffer view source");
                    const int bytes = static_cast<int>(parent->elements.size());
                    const int element_size = target_desc.find("IntBuffer")!=std::string::npos || target_desc.find("FloatBuffer")!=std::string::npos ? 4 : 2;
                    const ObjectRef view = heap_.allocate_object(target_desc.find("IntBuffer")!=std::string::npos ? "java/nio/IntBuffer" : (target_desc.find("FloatBuffer")!=std::string::npos ? "java/nio/FloatBuffer" : "java/nio/ShortBuffer"));
                    JvmValue direct_value; const bool is_direct = heap_.get_field(target_receiver, "buffer_direct", direct_value) && std::holds_alternative<std::int32_t>(direct_value) && std::get<std::int32_t>(direct_value) != 0;
                    if (!view || !heap_.put_field(view,"buffer_parent",target_receiver) || !heap_.put_field(view,"buffer_capacity",std::int32_t(bytes/element_size)) || !heap_.put_field(view,"buffer_limit",std::int32_t(bytes/element_size)) || !heap_.put_field(view,"buffer_position",std::int32_t(0)) || !heap_.put_field(view,"buffer_direct",std::int32_t(is_direct ? 1 : 0))) return bad("NIO typed view allocation failed");
                    if (!push(view)) return bad("NIO typed view stack overflow");
                    continue;
                }
                if (target_receiver && (owner == "java/nio/ByteBuffer" || owner == "java/nio/Buffer" || owner == "java/nio/IntBuffer" || owner == "java/nio/FloatBuffer" || owner == "java/nio/ShortBuffer")) {
                    auto* buffer = heap_.get(target_receiver);
                    if (!buffer) return bad("invalid NIO buffer receiver");
                    auto read_int = [&](const char* key, int fallback) { JvmValue v; return heap_.get_field(target_receiver,key,v)&&std::holds_alternative<std::int32_t>(v)?std::get<std::int32_t>(v):fallback; };
                    if (target_name == "isDirect" && target_desc == "()Z") { if(!push(std::int32_t(read_int("buffer_direct",0) != 0 ? 1 : 0)))return bad("Buffer.isDirect stack overflow"); continue; }
                    if (target_name == "remaining" && target_desc == "()I") { if(!push(std::int32_t(read_int("buffer_limit",static_cast<int>(buffer->elements.size()))-read_int("buffer_position",0))))return bad("Buffer.remaining stack overflow"); continue; }
                    if (target_name == "hasRemaining" && target_desc == "()Z") { if(!push(std::int32_t(read_int("buffer_position",0)<read_int("buffer_limit",static_cast<int>(buffer->elements.size()))?1:0)))return bad("Buffer.hasRemaining stack overflow"); continue; }
                    if (target_name == "capacity" && target_desc == "()I") { if(!push(std::int32_t(read_int("buffer_capacity",static_cast<int>(buffer->elements.size())))))return bad("Buffer.capacity stack overflow"); continue; }
                    if (target_name == "limit" && target_desc == "()I") { if(!push(std::int32_t(read_int("buffer_limit",static_cast<int>(buffer->elements.size())))))return bad("Buffer.limit stack overflow"); continue; }
                    if (target_name == "position" && target_desc == "()I") { if(!push(std::int32_t(read_int("buffer_position",0))))return bad("Buffer.position stack overflow"); continue; }
                    if (target_name == "position" && target_desc == "(I)Ljava/nio/Buffer;") { const int n=std::get<std::int32_t>(target_args[0]); if(n<0||n>read_int("buffer_limit",static_cast<int>(buffer->elements.size())))return bad("Buffer.position out of range"); buffer->fields["buffer_position"]=std::int32_t(n); if(!push(target_receiver))return bad("Buffer.position stack overflow"); continue; }
                    if (target_name == "limit" && target_desc == "(I)Ljava/nio/Buffer;") { const int n=std::get<std::int32_t>(target_args[0]); if(n<0||n>read_int("buffer_capacity",static_cast<int>(buffer->elements.size())))return bad("Buffer.limit out of range"); buffer->fields["buffer_limit"]=std::int32_t(n); if(read_int("buffer_position",0)>n)buffer->fields["buffer_position"]=std::int32_t(n); if(!push(target_receiver))return bad("Buffer.limit stack overflow"); continue; }
                    if (target_name == "clear" && target_desc == "()Ljava/nio/Buffer;") { buffer->fields["buffer_position"]=std::int32_t(0); buffer->fields["buffer_limit"]=std::int32_t(read_int("buffer_capacity",static_cast<int>(buffer->elements.size()))); if(!push(target_receiver))return bad("Buffer.clear stack overflow"); continue; }
                    if (target_name == "flip" && target_desc == "()Ljava/nio/Buffer;") { buffer->fields["buffer_limit"]=std::int32_t(read_int("buffer_position",0)); buffer->fields["buffer_position"]=std::int32_t(0); if(!push(target_receiver))return bad("Buffer.flip stack overflow"); continue; }
                    if (target_name == "rewind" && target_desc == "()Ljava/nio/Buffer;") { buffer->fields["buffer_position"]=std::int32_t(0); if(!push(target_receiver))return bad("Buffer.rewind stack overflow"); continue; }
                    if (target_name == "compact" && target_desc == "()Ljava/nio/ByteBuffer;") {
                        const int position = read_int("buffer_position", 0);
                        const int limit = read_int("buffer_limit", static_cast<int>(buffer->elements.size()));
                        const int capacity = read_int("buffer_capacity", static_cast<int>(buffer->elements.size()));
                        if (position < 0 || limit < position || limit > static_cast<int>(buffer->elements.size())) return bad("ByteBuffer.compact bounds");
                        const int remaining = limit - position;
                        for (int i = 0; i < remaining; ++i) buffer->elements[static_cast<std::size_t>(i)] = buffer->elements[static_cast<std::size_t>(position + i)];
                        for (int i = remaining; i < capacity && static_cast<std::size_t>(i) < buffer->elements.size(); ++i) buffer->elements[static_cast<std::size_t>(i)] = std::int32_t(0);
                        buffer->fields["buffer_position"] = std::int32_t(remaining);
                        buffer->fields["buffer_limit"] = std::int32_t(capacity);
                        if (!push(target_receiver)) return bad("ByteBuffer.compact stack overflow");
                        continue;
                    }
                    if ((target_name == "asReadOnlyBuffer" || target_name == "duplicate" || target_name == "slice") &&
                        target_desc == "()Ljava/nio/ByteBuffer;") {
                        if (!push(target_receiver)) return bad("ByteBuffer view stack overflow");
                        continue;
                    }
                    if (target_name == "order" && target_desc == "(Ljava/nio/ByteOrder;)Ljava/nio/ByteBuffer;") { if(!push(target_receiver))return bad("ByteBuffer.order stack overflow"); continue; }
                    if (target_name == "get" && target_desc == "()B") { int pos=read_int("buffer_position",0), lim=read_int("buffer_limit",static_cast<int>(buffer->elements.size())); if(pos>=lim)return bad("Buffer.get underflow"); JvmValue v=buffer->elements[static_cast<std::size_t>(pos)]; buffer->fields["buffer_position"]=std::int32_t(pos+1); if(!push(v))return bad("Buffer.get stack overflow"); continue; }
                    if (target_name == "getInt" && (target_desc == "(I)I" || target_desc == "()I")) {
                        int pos=target_desc=="(I)I"?std::get<std::int32_t>(target_args[0]):read_int("buffer_position",0);
                        int lim=read_int("buffer_limit",static_cast<int>(buffer->elements.size()));if(pos<0||pos+4>lim)return bad("ByteBuffer.getInt bounds");
                        std::uint32_t value=0;for(int b=0;b<4;++b)value|=(static_cast<std::uint32_t>(std::get<std::int32_t>(buffer->elements[static_cast<std::size_t>(pos+b)])&0xff)<<(b*8));
                        if(target_desc=="()I")buffer->fields["buffer_position"]=std::int32_t(pos+4);
                        if(!push(static_cast<std::int32_t>(value)))return bad("ByteBuffer.getInt stack overflow");
                        continue;
                    }
                    if (target_name == "putInt" && (target_desc == "(I)Ljava/nio/ByteBuffer;" || target_desc == "(II)Ljava/nio/ByteBuffer;")) {
                        int pos=target_desc=="(II)Ljava/nio/ByteBuffer;"?std::get<std::int32_t>(target_args[0]):read_int("buffer_position",0);
                        int arg=target_desc=="(II)Ljava/nio/ByteBuffer;"?1:0;int lim=read_int("buffer_limit",static_cast<int>(buffer->elements.size()));if(pos<0||pos+4>lim)return bad("ByteBuffer.putInt bounds");
                        const auto value=static_cast<std::uint32_t>(std::get<std::int32_t>(target_args[arg]));for(int b=0;b<4;++b)buffer->elements[static_cast<std::size_t>(pos+b)]=std::int32_t((value>>(b*8))&0xffu);
                        if(target_desc=="(I)Ljava/nio/ByteBuffer;")buffer->fields["buffer_position"]=std::int32_t(pos+4);
                        if(!push(target_receiver))return bad("ByteBuffer.putInt stack overflow");
                        continue;
                    }
                    if (target_name == "put" && target_desc == "(IB)Ljava/nio/ByteBuffer;") {
                        const int index = std::get<std::int32_t>(target_args[0]);
                        const int capacity = read_int("buffer_limit", static_cast<int>(buffer->elements.size()));
                        if (index < 0 || index >= capacity) return bad("ByteBuffer.put(index, byte) bounds");
                        buffer->elements[static_cast<std::size_t>(index)] = target_args[1];
                        if (!push(target_receiver)) return bad("ByteBuffer absolute put stack overflow");
                        continue;
                    }
                    if (target_name == "put" && target_desc == "(B)Ljava/nio/ByteBuffer;") { int pos=read_int("buffer_position",0),lim=read_int("buffer_limit",static_cast<int>(buffer->elements.size())); if(pos>=lim)return bad("Buffer.put overflow"); buffer->elements[static_cast<std::size_t>(pos)]=target_args[0]; buffer->fields["buffer_position"]=std::int32_t(pos+1); if(!push(target_receiver))return bad("Buffer.put stack overflow"); continue; }
                    if (target_name == "put" && (target_desc == "([B)Ljava/nio/ByteBuffer;" || target_desc == "([BII)Ljava/nio/ByteBuffer;")) {
                        ObjectRef src_ref=0;as_ref(target_args[0],src_ref);const auto* src=heap_.get(src_ref);if(!src||src->kind!=HeapKind::PrimitiveArray)return bad("ByteBuffer.put requires byte array");
                        const int src_offset=target_desc=="([BII)Ljava/nio/ByteBuffer;"?std::get<std::int32_t>(target_args[1]):0;
                        const int count=target_desc=="([BII)Ljava/nio/ByteBuffer;"?std::get<std::int32_t>(target_args[2]):static_cast<int>(src->elements.size());
                        const int pos=read_int("buffer_position",0),lim=read_int("buffer_limit",static_cast<int>(buffer->elements.size()));
                        if(src_offset<0||count<0||static_cast<std::size_t>(src_offset+count)>src->elements.size()||count>lim-pos)return bad("ByteBuffer.put byte array bounds");
                        for(int n=0;n<count;++n)buffer->elements[static_cast<std::size_t>(pos+n)]=src->elements[static_cast<std::size_t>(src_offset+n)];
                        buffer->fields["buffer_position"]=std::int32_t(pos+count);if(!push(target_receiver))return bad("ByteBuffer.put array stack overflow");continue;
                    }
                    if (buffer->class_name == "java/nio/FloatBuffer" && target_name == "put" && target_desc == "(IF)Ljava/nio/FloatBuffer;") {
                        const int index=std::get<std::int32_t>(target_args[0]); JvmValue parent_value; ObjectRef parent_ref=0;
                        if(index<0||index>=read_int("buffer_limit",0)||!heap_.get_field(target_receiver,"buffer_parent",parent_value)||!as_ref(parent_value,parent_ref))return bad("FloatBuffer absolute put bounds");
                        auto* parent=heap_.get(parent_ref);const std::size_t k=static_cast<std::size_t>(index)*4u;if(!parent||k+4>parent->elements.size())return bad("FloatBuffer absolute put backing range");
                        std::uint32_t bits=0;const float value=std::get<float>(target_args[1]);std::memcpy(&bits,&value,sizeof(bits));for(int b=0;b<4;++b)parent->elements[k+b]=std::int32_t((bits>>(b*8))&0xffu);
                        if (!push(target_receiver)) return bad("FloatBuffer absolute put stack overflow");
                        continue;
                    }
                    if (buffer->class_name == "java/nio/FloatBuffer" && target_name == "get" && target_desc == "(I)F") {
                        const int index=std::get<std::int32_t>(target_args[0]);JvmValue parent_value;ObjectRef parent_ref=0;
                        if(index<0||index>=read_int("buffer_limit",0)||!heap_.get_field(target_receiver,"buffer_parent",parent_value)||!as_ref(parent_value,parent_ref))return bad("FloatBuffer absolute get bounds");
                        const auto* parent=heap_.get(parent_ref);const std::size_t k=static_cast<std::size_t>(index)*4u;if(!parent||k+4>parent->elements.size())return bad("FloatBuffer absolute get backing range");
                        std::uint32_t bits=0;for(int b=0;b<4;++b)bits|=(static_cast<std::uint32_t>(std::get<std::int32_t>(parent->elements[k+b])&0xff)<<(b*8));float value=0;std::memcpy(&value,&bits,sizeof(value));
                        if (!push(value)) return bad("FloatBuffer absolute get stack overflow");
                        continue;
                    }
                    if (buffer->class_name == "java/nio/FloatBuffer" && target_name == "put" && target_desc == "(F)Ljava/nio/FloatBuffer;") {
                        int pos=read_int("buffer_position",0),lim=read_int("buffer_limit",0); JvmValue parent_value; ObjectRef parent_ref=0;
                        if(pos>=lim||!heap_.get_field(target_receiver,"buffer_parent",parent_value)||!as_ref(parent_value,parent_ref))return bad("FloatBuffer.put overflow");
                        auto* parent=heap_.get(parent_ref); if(!parent)return bad("FloatBuffer backing buffer missing");
                        std::uint32_t bits=0; const float value=std::get<float>(target_args[0]); std::memcpy(&bits,&value,sizeof(bits));
                        const std::size_t k=static_cast<std::size_t>(pos)*4u; if(k+4>parent->elements.size())return bad("FloatBuffer backing range invalid");
                        for(int b=0;b<4;++b)parent->elements[k+b]=std::int32_t((bits>>(b*8))&0xffu);
                        buffer->fields["buffer_position"]=std::int32_t(pos+1); if(!push(target_receiver))return bad("FloatBuffer.put stack overflow"); continue;
                    }
                    if (buffer->class_name == "java/nio/FloatBuffer" && target_name == "put" && (target_desc == "([F)Ljava/nio/FloatBuffer;" || target_desc == "([FII)Ljava/nio/FloatBuffer;")) {
                        ObjectRef source_ref=0; as_ref(target_args[0],source_ref); const auto* source=heap_.get(source_ref);
                        const int offset=target_desc=="([FII)Ljava/nio/FloatBuffer;"?std::get<std::int32_t>(target_args[1]):0;
                        const int count=target_desc=="([FII)Ljava/nio/FloatBuffer;"?std::get<std::int32_t>(target_args[2]):(source?static_cast<int>(source->elements.size()):0);
                        int pos=read_int("buffer_position",0),lim=read_int("buffer_limit",0); JvmValue parent_value; ObjectRef parent_ref=0;
                        if(!source||offset<0||count<0||static_cast<std::size_t>(offset+count)>source->elements.size()||count>lim-pos||!heap_.get_field(target_receiver,"buffer_parent",parent_value)||!as_ref(parent_value,parent_ref))return bad("FloatBuffer.put array bounds");
                        auto* parent=heap_.get(parent_ref); if(!parent)return bad("FloatBuffer.put backing buffer missing");
                        for(int n=0;n<count;++n){if(!std::holds_alternative<float>(source->elements[static_cast<std::size_t>(offset+n)]))return bad("FloatBuffer.put source type");std::uint32_t bits=0;const float value=std::get<float>(source->elements[static_cast<std::size_t>(offset+n)]);std::memcpy(&bits,&value,sizeof(bits));const std::size_t k=static_cast<std::size_t>(pos+n)*4u;for(int b=0;b<4;++b)parent->elements[k+b]=std::int32_t((bits>>(b*8))&0xffu);}
                        buffer->fields["buffer_position"]=std::int32_t(pos+count); if(!push(target_receiver))return bad("FloatBuffer.put array stack overflow"); continue;
                    }
                    if (buffer->class_name == "java/nio/FloatBuffer" && target_name == "get" && target_desc == "()F") {
                        int pos=read_int("buffer_position",0),lim=read_int("buffer_limit",0); JvmValue parent_value; ObjectRef parent_ref=0;
                        if(pos>=lim||!heap_.get_field(target_receiver,"buffer_parent",parent_value)||!as_ref(parent_value,parent_ref))return bad("FloatBuffer.get underflow");
                        const auto* parent=heap_.get(parent_ref);const std::size_t k=static_cast<std::size_t>(pos)*4u;if(!parent||k+4>parent->elements.size())return bad("FloatBuffer.get backing range invalid");
                        std::uint32_t bits=0;for(int b=0;b<4;++b)bits|=(static_cast<std::uint32_t>(std::get<std::int32_t>(parent->elements[k+b])&0xff)<<(b*8));float value=0;std::memcpy(&value,&bits,sizeof(value));
                        buffer->fields["buffer_position"]=std::int32_t(pos+1);if(!push(value))return bad("FloatBuffer.get stack overflow");continue;
                    }
                    if (buffer->class_name == "java/nio/IntBuffer" && target_name == "get" && target_desc == "()I") {
                        int pos=read_int("buffer_position",0),lim=read_int("buffer_limit",0); JvmValue parent_value; ObjectRef parent_ref=0;
                        if(pos>=lim || !heap_.get_field(target_receiver,"buffer_parent",parent_value) || !as_ref(parent_value,parent_ref)) return bad("IntBuffer.get underflow");
                        auto* parent=heap_.get(parent_ref); const std::size_t k=static_cast<std::size_t>(pos)*4u; if(!parent||k+4>parent->elements.size())return bad("IntBuffer backing range invalid");
                        std::uint32_t value=0; for(int b=0;b<4;++b) value|=(static_cast<std::uint32_t>(std::get<std::int32_t>(parent->elements[k+b])&0xff)<<(b*8));
                        buffer->fields["buffer_position"]=std::int32_t(pos+1); if(!push(static_cast<std::int32_t>(value)))return bad("IntBuffer.get stack overflow"); continue;
                    }
                    if (buffer->class_name == "java/nio/IntBuffer" && target_name == "get" && target_desc == "(I)I") {
                        const int index=std::get<std::int32_t>(target_args[0]); JvmValue parent_value; ObjectRef parent_ref=0;
                        if(index<0||index>=read_int("buffer_limit",0)||!heap_.get_field(target_receiver,"buffer_parent",parent_value)||!as_ref(parent_value,parent_ref))return bad("IntBuffer.get(index) bounds");
                        auto* parent=heap_.get(parent_ref); const std::size_t k=static_cast<std::size_t>(index)*4u; if(!parent||k+4>parent->elements.size())return bad("IntBuffer backing range invalid");
                        std::uint32_t value=0;for(int b=0;b<4;++b)value|=(static_cast<std::uint32_t>(std::get<std::int32_t>(parent->elements[k+b])&0xff)<<(b*8));
                        if(!push(static_cast<std::int32_t>(value)))return bad("IntBuffer.get(index) stack overflow");
                        continue;
                    }
                    if (buffer->class_name == "java/nio/IntBuffer" && target_name == "put" && target_desc == "([III)Ljava/nio/IntBuffer;") {
                        ObjectRef src_ref=0;as_ref(target_args[0],src_ref);const auto* src=heap_.get(src_ref);if(!src)return bad("IntBuffer.put requires int array");
                        const int offset=std::get<std::int32_t>(target_args[1]),count=std::get<std::int32_t>(target_args[2]);int pos=read_int("buffer_position",0),lim=read_int("buffer_limit",0);JvmValue pv;ObjectRef parent_ref=0;
                        if(offset<0||count<0||static_cast<std::size_t>(offset+count)>src->elements.size()||count>lim-pos||!heap_.get_field(target_receiver,"buffer_parent",pv)||!as_ref(pv,parent_ref))return bad("IntBuffer.put array bounds");
                        auto* parent=heap_.get(parent_ref);if(!parent)return bad("IntBuffer.put backing buffer missing");
                        for(int n=0;n<count;++n){const auto value=static_cast<std::uint32_t>(std::get<std::int32_t>(src->elements[static_cast<std::size_t>(offset+n)]));const std::size_t k=static_cast<std::size_t>(pos+n)*4u;for(int b=0;b<4;++b)parent->elements[k+b]=std::int32_t((value>>(b*8))&0xffu);}
                        buffer->fields["buffer_position"]=std::int32_t(pos+count);if(!push(target_receiver))return bad("IntBuffer.put array stack overflow");continue;
                    }
                    if (buffer->class_name == "java/nio/IntBuffer" && target_name == "put" && target_desc == "(I)Ljava/nio/IntBuffer;") {
                        int pos=read_int("buffer_position",0),lim=read_int("buffer_limit",0); JvmValue parent_value; ObjectRef parent_ref=0;
                        if(pos>=lim || !heap_.get_field(target_receiver,"buffer_parent",parent_value) || !as_ref(parent_value,parent_ref)) return bad("IntBuffer.put overflow");
                        auto* parent=heap_.get(parent_ref); const std::size_t k=static_cast<std::size_t>(pos)*4u; if(!parent||k+4>parent->elements.size())return bad("IntBuffer backing range invalid");
                        const auto value=static_cast<std::uint32_t>(std::get<std::int32_t>(target_args[0])); for(int b=0;b<4;++b)parent->elements[k+b]=std::int32_t((value>>(b*8))&0xffu);
                        buffer->fields["buffer_position"]=std::int32_t(pos+1); if(!push(target_receiver))return bad("IntBuffer.put stack overflow"); continue;
                    }
                }
                if (owner == "javax/imageio/ImageIO" && target_name == "read" &&
                    (target_desc == "(Ljava/net/URL;)Ljava/awt/image/BufferedImage;" || target_desc == "(Ljava/io/InputStream;)Ljava/awt/image/BufferedImage;") && op == 0xb8) {
                    ObjectRef source_ref = 0; if (!as_ref(target_args[0], source_ref) || !source_ref) {
                        if (!push(NullValue{})) return bad("ImageIO.read source stack overflow");
                        continue;
                    }
                    const auto* source_object = heap_.get(source_ref);
                    std::vector<std::uint8_t> png_bytes;
                    if (target_desc.find("URL") != std::string::npos) {
                        if (!source_object || source_object->string_value.rfind("resource:/", 0) != 0) {
                            if (diagnostics_.size() < 64000) diagnostics_ += "IMAGEIO_BAD_URL=" + (source_object ? source_object->string_value : std::string("<missing>")) + "\n";
                            if (!push(NullValue{})) return bad("ImageIO.read null URL stack overflow");
                            continue;
                        }
                        const std::string resource_name = source_object->string_value.substr(10);
                        if (!classes_ || !classes_->find_resource(resource_name, png_bytes)) {
                            if (diagnostics_.size() < 64000) diagnostics_ += "IMAGEIO_RESOURCE_MISSING=" + resource_name + "\n";
                            png_bytes.clear();
                        }
                    } else if (source_object) {
                        png_bytes.assign(source_object->stream_bytes.begin()+static_cast<std::ptrdiff_t>(std::min(source_object->stream_position,source_object->stream_bytes.size())),source_object->stream_bytes.end());
                    }
                    if (png_bytes.empty()) {
                        if (diagnostics_.size() < 64000) diagnostics_ += "IMAGEIO_EMPTY_RESOURCE\n";
                        if (!push(NullValue{})) return bad("ImageIO.read missing resource stack overflow");
                        continue;
                    }
                    png_image image{}; image.version = PNG_IMAGE_VERSION;
                    if (!png_image_begin_read_from_memory(&image, png_bytes.data(), png_bytes.size())) {
                        if (diagnostics_.size() < 64000) diagnostics_ += "IMAGEIO_PNG_HEADER_FAIL\n";
                        if (!push(NullValue{})) return bad("ImageIO.read decode failure stack overflow");
                        continue;
                    }
                    image.format = PNG_FORMAT_RGBA;
                    std::vector<std::uint8_t> rgba(PNG_IMAGE_SIZE(image));
                    if (!png_image_finish_read(&image, nullptr, rgba.data(), 0, nullptr)) {
                        if (diagnostics_.size() < 64000) diagnostics_ += "IMAGEIO_PNG_DECODE_FAIL\n";
                        png_image_free(&image);
                        if (!push(NullValue{})) return bad("ImageIO.read PNG decode stack overflow");
                        continue;
                    }
                    const std::size_t pixel_count = static_cast<std::size_t>(image.width) * image.height;
                    const ObjectRef pixel_array = heap_.allocate_int_array(pixel_count);
                    const ObjectRef buffered = heap_.allocate_object("java/awt/image/BufferedImage");
                    if (!pixel_array || !buffered) { png_image_free(&image); return bad("ImageIO.read image allocation failed"); }
                    for (std::size_t n = 0; n < pixel_count; ++n) {
                        const std::size_t k = n * 4;
                        const std::uint32_t argb = (static_cast<std::uint32_t>(rgba[k+3]) << 24) |
                            (static_cast<std::uint32_t>(rgba[k]) << 16) |
                            (static_cast<std::uint32_t>(rgba[k+1]) << 8) | rgba[k+2];
                        if (!heap_.array_store(pixel_array, n, static_cast<std::int32_t>(argb))) {
                            png_image_free(&image); return bad("ImageIO.read pixel store failed");
                        }
                    }
                    auto* image_object = heap_.get(buffered);
                    image_object->fields["image_width"] = static_cast<std::int32_t>(image.width);
                    image_object->fields["image_height"] = static_cast<std::int32_t>(image.height);
                    image_object->fields["image_pixels"] = pixel_array;
                    png_image_free(&image);
                    if (!push(buffered)) return bad("ImageIO.read result stack overflow");
                    continue;
                }
                if (target_receiver && owner == "java/awt/image/BufferedImage") {
                    auto* image_object = heap_.get(target_receiver);
                    if (!image_object) return bad("invalid BufferedImage receiver");
                    if (target_name == "<init>" && target_desc == "(III)V") {
                        const int w=std::get<std::int32_t>(target_args[0]), h=std::get<std::int32_t>(target_args[1]);
                        if(w<=0||h<=0||static_cast<std::uint64_t>(w)*static_cast<std::uint64_t>(h)>16000000u)return bad("BufferedImage dimensions invalid");
                        const ObjectRef pixels=heap_.allocate_int_array(static_cast<std::size_t>(w)*static_cast<std::size_t>(h));
                        if(!pixels)return bad("BufferedImage pixel allocation failed");
                        image_object->fields["image_width"]=std::int32_t(w); image_object->fields["image_height"]=std::int32_t(h); image_object->fields["image_pixels"]=pixels;
                        continue;
                    }
                    auto image_int = [&](const char* key) -> std::int32_t { JvmValue v; return heap_.get_field(target_receiver,key,v) && std::holds_alternative<std::int32_t>(v) ? std::get<std::int32_t>(v) : 0; };
                    if (target_name == "getGraphics" && target_desc == "()Ljava/awt/Graphics;") {
                        const ObjectRef graphics=heap_.allocate_object("java/awt/Graphics");
                        if(!graphics||!heap_.put_field(graphics,"graphics_image",target_receiver)||!push(graphics))return bad("BufferedImage.getGraphics allocation failed");
                        continue;
                    }
                    if (target_name == "getWidth" && target_desc == "()I") { if (!push(image_int("image_width"))) return bad("BufferedImage.getWidth stack overflow"); continue; }
                    if (target_name == "getHeight" && target_desc == "()I") { if (!push(image_int("image_height"))) return bad("BufferedImage.getHeight stack overflow"); continue; }
                    if (target_name == "getRGB" && target_desc == "(II)I") {
                        const int x = std::get<std::int32_t>(target_args[0]), yv = std::get<std::int32_t>(target_args[1]);
                        const int w = image_int("image_width"), h = image_int("image_height"); JvmValue pixels_value; ObjectRef pixels = 0;
                        if (x < 0 || yv < 0 || x >= w || yv >= h || !heap_.get_field(target_receiver,"image_pixels",pixels_value) || !as_ref(pixels_value,pixels)) return bad("BufferedImage.getRGB coordinates out of bounds");
                        JvmValue pixel; if (!heap_.array_load(pixels,static_cast<std::size_t>(yv)*w+x,pixel) || !push(pixel)) return bad("BufferedImage.getRGB pixel read failed"); continue;
                    }
                    if (target_name == "getRGB" && target_desc == "(IIII[III)[I") {
                        const int x=std::get<std::int32_t>(target_args[0]), yv=std::get<std::int32_t>(target_args[1]);
                        const int rw=std::get<std::int32_t>(target_args[2]), rh=std::get<std::int32_t>(target_args[3]);
                        ObjectRef dst=0; as_ref(target_args[4],dst); const int offset=std::get<std::int32_t>(target_args[5]), scan=std::get<std::int32_t>(target_args[6]);
                        JvmValue pixels_value; ObjectRef pixels=0; const int w=image_int("image_width"),h=image_int("image_height");
                        if (!dst || !heap_.get_field(target_receiver,"image_pixels",pixels_value) || !as_ref(pixels_value,pixels) || x<0 || yv<0 || rw<0 || rh<0 || x+rw>w || yv+rh>h) return bad("BufferedImage.getRGB region invalid");
                        for(int row=0;row<rh;++row) for(int col=0;col<rw;++col) { JvmValue px; if(!heap_.array_load(pixels,static_cast<std::size_t>(yv+row)*w+x+col,px) || !heap_.array_store(dst,static_cast<std::size_t>(offset+row*scan+col),px)) return bad("BufferedImage.getRGB bulk copy failed"); }
                        if (!push(dst)) return bad("BufferedImage.getRGB bulk stack overflow");
                        continue;
                    }
                }
                if (target_receiver && receiver_class == "java/lang/Class" && target_name == "equals" &&
                    target_desc == "(Ljava/lang/Object;)Z") {
                    ObjectRef other_ref = 0; as_ref(target_args[0], other_ref);
                    const auto* left_class = heap_.get(target_receiver);
                    const auto* right_class = other_ref ? heap_.get(other_ref) : nullptr;
                    const bool equal = left_class && right_class && right_class->class_name == "java/lang/Class" &&
                        left_class->string_value == right_class->string_value;
                    if (!push(std::int32_t(equal ? 1 : 0))) return bad("Class.equals stack overflow");
                    continue;
                }
                if (target_receiver && owner == "java/lang/Class") {
                    if ((target_name == "getName" || target_name == "getCanonicalName" || target_name == "getSimpleName") &&
                        target_desc == "()Ljava/lang/String;") {
                        const auto* class_object = heap_.get(target_receiver);
                        if (!class_object) return bad("Class.getName receiver missing");
                        std::string name = class_object->string_value;
                        if (target_name == "getSimpleName") {
                            const auto slash = name.rfind('/');
                            if (slash != std::string::npos) name.erase(0, slash + 1);
                            const auto dollar = name.rfind('$');
                            if (dollar != std::string::npos) name.erase(0, dollar + 1);
                        } else if (target_name == "getName") {
                            std::replace(name.begin(), name.end(), '/', '.');
                        }
                        const ObjectRef result = heap_.allocate_string(name);
                        if (!result || !push(result)) return bad("Class.getName result allocation failed");
                        continue;
                    }
                    if ((target_name == "getClassLoader" || target_name == "getClassLoader0") &&
                        target_desc == "()Ljava/lang/ClassLoader;") {
                        if (!push(NullValue{})) return bad("Class.getClassLoader stack overflow");
                        continue;
                    }
                    if (target_name == "desiredAssertionStatus" && target_desc == "()Z") {
                        if (!push(std::int32_t(0))) return bad("Class.desiredAssertionStatus stack overflow");
                        continue;
                    }
                    if (target_name == "getFields" && target_desc == "()[Ljava/lang/reflect/Field;") {
                        const auto* class_object = heap_.get(target_receiver);
                        if (!class_object || !classes_) return bad("Class.getFields runtime unavailable");
                        std::vector<std::pair<std::string, FieldInfo>> public_fields;
                        std::unordered_set<std::string> visited;
                        std::function<void(const std::string&)> collect_public_fields = [&](const std::string& current) {
                            if (current.empty() || !visited.insert(current).second) return;
                            const auto* info = classes_->find_class(current);
                            if (!info) return;
                            for (const auto& field : info->fields) {
                                if (field.access_flags & 0x0001) public_fields.emplace_back(current, field);
                            }
                            collect_public_fields(info->super_class);
                            for (const auto& interface_name : info->interfaces) collect_public_fields(interface_name);
                        };
                        collect_public_fields(class_object->string_value);
                        const ObjectRef fields_array = heap_.allocate_reference_array(public_fields.size(), "Ljava/lang/reflect/Field;");
                        if (!fields_array) return bad("Class.getFields array allocation failed");
                        for (std::size_t i = 0; i < public_fields.size(); ++i) {
                            const auto& entry = public_fields[i];
                            const ObjectRef field_ref = heap_.allocate_object("java/lang/reflect/Field");
                            const ObjectRef declaring_ref = heap_.allocate_string(entry.first);
                            const ObjectRef name_ref = heap_.allocate_string(entry.second.name);
                            const ObjectRef descriptor_ref = heap_.allocate_string(entry.second.descriptor);
                            if (!field_ref || !declaring_ref || !name_ref || !descriptor_ref ||
                                !heap_.put_field(field_ref, "reflect_declaring_class", declaring_ref) ||
                                !heap_.put_field(field_ref, "reflect_field_name", name_ref) ||
                                !heap_.put_field(field_ref, "reflect_field_descriptor", descriptor_ref) ||
                                !heap_.put_field(field_ref, "reflect_field_modifiers", std::int32_t(entry.second.access_flags)) ||
                                !heap_.put_field(field_ref, "reflect_field_static", std::int32_t((entry.second.access_flags & 0x0008) ? 1 : 0)) ||
                                !heap_.array_store(fields_array, i, field_ref))
                                return bad("Class.getFields metadata allocation failed");
                        }
                        if (!push(fields_array)) return bad("Class.getFields stack overflow");
                        continue;
                    }
                }
                if (owner == "sun/reflect/Reflection" && op == 0xb8 && target_name == "getCallerClass" &&
                    target_desc == "(I)Ljava/lang/Class;") {
                    const ObjectRef caller_class = heap_.allocate_object("java/lang/Class");
                    auto* class_object = heap_.get(caller_class);
                    if (!caller_class || !class_object) return bad("Reflection.getCallerClass allocation failed");
                    class_object->string_value = "java/lang/ThreadLocal";
                    if (!push(caller_class)) return bad("Reflection.getCallerClass stack overflow");
                    continue;
                }
                if (owner == "java/lang/ClassLoader" && op == 0xb8 && target_name == "getCallerClassLoader" &&
                    target_desc == "()Ljava/lang/ClassLoader;") {
                    if (!push(NullValue{})) return bad("ClassLoader.getCallerClassLoader stack overflow");
                    continue;
                }
                if (owner == "java/lang/Class" && op == 0xb8 && target_name == "getPrimitiveClass" &&
                    target_desc == "(Ljava/lang/String;)Ljava/lang/Class;") {
                    ObjectRef name_ref = 0;
                    std::string primitive_name;
                    if (target_args.empty() || !as_ref(target_args[0], name_ref) || !name_ref ||
                        !heap_.get_string(name_ref, primitive_name)) return bad("Class.getPrimitiveClass requires a String");
                    const ObjectRef class_ref = heap_.allocate_object("java/lang/Class");
                    auto* class_object = heap_.get(class_ref);
                    if (!class_ref || !class_object) return bad("primitive Class allocation failed");
                    class_object->string_value = primitive_name;
                    if (!push(class_ref)) return bad("Class.getPrimitiveClass stack overflow");
                    continue;
                }
                if (op == 0xb8 && target_name == "initIDs" && target_desc == "()V" &&
                    owner.rfind("java/awt/", 0) == 0) {
                    // AWT native IDs are not used by the software/host AWT bridge.
                    continue;
                }
                if (target_receiver && owner == "java/lang/Class" && target_name == "isAssignableFrom" &&
                    target_desc == "(Ljava/lang/Class;)Z") {
                    const auto* target_class_object = heap_.get(target_receiver);
                    ObjectRef source_ref = 0;
                    if (!target_class_object || !as_ref(target_args[0], source_ref) || !source_ref) return bad("Class.isAssignableFrom arguments invalid");
                    const auto* source_class_object = heap_.get(source_ref);
                    if (!source_class_object) return bad("Class.isAssignableFrom source Class missing");
                    const std::string wanted = target_class_object->string_value;
                    const std::string source = source_class_object->string_value;
                    std::unordered_set<std::string> visited;
                    std::function<bool(const std::string&)> assignable_from = [&](const std::string& current) -> bool {
                        if (current == wanted) return true;
                        if (!classes_ || current.empty() || !visited.insert(current).second) return false;
                        const auto* info = classes_->find_class(current);
                        if (!info) return false;
                        if (assignable_from(info->super_class)) return true;
                        for (const auto& interface_name : info->interfaces)
                            if (assignable_from(interface_name)) return true;
                        return false;
                    };
                    const bool assignable = assignable_from(source);
                    if (!push(std::int32_t(assignable ? 1 : 0))) return bad("Class.isAssignableFrom stack overflow");
                    continue;
                }
                if (owner == "java/awt/Toolkit" && op == 0xb8 && target_name == "loadLibraries" && target_desc == "()V") {
                    // Desktop AWT native libraries are replaced by the platform host adapter.
                    continue;
                }
                // Reflection subset used by the original LWJGL/JRE bootstrap.
                // Class objects keep their internal JVM class name in string_value.
                if (target_receiver && owner == "java/lang/Class" && target_name == "getDeclaredField" &&
                    target_desc == "(Ljava/lang/String;)Ljava/lang/reflect/Field;") {
                    auto* class_object = heap_.get(target_receiver);
                    ObjectRef name_ref = 0; std::string field_name;
                    if (!class_object || !as_ref(target_args[0], name_ref) || !name_ref ||
                        !heap_.get_string(name_ref, field_name)) return bad("Class.getDeclaredField requires a field name");
                    const auto* declared = classes_ ? classes_->find_class(class_object->string_value) : nullptr;
                    if (!declared) return bad("Class.getDeclaredField class is not registered: " + class_object->string_value);
                    const auto it = std::find_if(declared->fields.begin(), declared->fields.end(),
                        [&](const FieldInfo& f) { return f.name == field_name; });
                    if (it == declared->fields.end()) return bad("NoSuchFieldException: " + class_object->string_value + "." + field_name);
                    const ObjectRef field_ref = heap_.allocate_object("java/lang/reflect/Field");
                    if (!field_ref ||
                        !heap_.put_field(field_ref, "reflect_declaring_class", heap_.allocate_string(class_object->string_value)) ||
                        !heap_.put_field(field_ref, "reflect_field_name", heap_.allocate_string(it->name)) ||
                        !heap_.put_field(field_ref, "reflect_field_descriptor", heap_.allocate_string(it->descriptor)) ||
                        !heap_.put_field(field_ref, "reflect_field_modifiers", std::int32_t(it->access_flags)) ||
                        !heap_.put_field(field_ref, "reflect_field_static", std::int32_t((it->access_flags & 0x0008) ? 1 : 0)))
                        return bad("Class.getDeclaredField metadata allocation failed");
                    if (!push(field_ref)) return bad("Class.getDeclaredField stack overflow");
                    continue;
                }
                if (target_receiver && owner == "java/lang/reflect/Field") {
                    auto* field_object = heap_.get(target_receiver);
                    auto read_string = [&](const char* key, std::string& value) {
                        JvmValue v; ObjectRef r = 0;
                        return heap_.get_field(target_receiver, key, v) && as_ref(v, r) && r && heap_.get_string(r, value);
                    };
                    std::string declaring, field_name, descriptor;
                    if (!field_object || !read_string("reflect_declaring_class", declaring) ||
                        !read_string("reflect_field_name", field_name) ||
                        !read_string("reflect_field_descriptor", descriptor)) return bad("invalid reflected Field metadata");
                    JvmValue static_flag; const bool is_static = heap_.get_field(target_receiver, "reflect_field_static", static_flag) &&
                        std::holds_alternative<std::int32_t>(static_flag) && std::get<std::int32_t>(static_flag) != 0;
                    if (target_name == "setAccessible" && target_desc == "(Z)V") continue;
                    if (target_name == "getName" && target_desc == "()Ljava/lang/String;") {
                        const ObjectRef value = heap_.allocate_string(field_name);
                        if (!value || !push(value)) return bad("Field.getName allocation failed");
                        continue;
                    }
                    if (target_name == "getType" && target_desc == "()Ljava/lang/Class;") {
                        std::string type_name;
                        if (descriptor == "I") type_name = "int";
                        else if (descriptor == "Z") type_name = "boolean";
                        else if (descriptor == "B") type_name = "byte";
                        else if (descriptor == "C") type_name = "char";
                        else if (descriptor == "S") type_name = "short";
                        else if (descriptor == "J") type_name = "long";
                        else if (descriptor == "F") type_name = "float";
                        else if (descriptor == "D") type_name = "double";
                        else if (!descriptor.empty() && descriptor.front() == 'L' && descriptor.back() == ';') type_name = descriptor.substr(1, descriptor.size() - 2);
                        else type_name = descriptor;
                        const ObjectRef type_ref = heap_.allocate_object("java/lang/Class");
                        auto* type_object = heap_.get(type_ref);
                        if (!type_ref || !type_object) return bad("Field.getType Class allocation failed");
                        type_object->string_value = type_name;
                        if (!push(type_ref)) return bad("Field.getType stack overflow");
                        continue;
                    }
                    if (target_name == "getModifiers" && target_desc == "()I") {
                        JvmValue modifiers; if (!heap_.get_field(target_receiver, "reflect_field_modifiers", modifiers) || !push(modifiers)) return bad("Field.getModifiers failed");
                        continue;
                    }
                    if (target_name == "get" && target_desc == "(Ljava/lang/Object;)Ljava/lang/Object;") {
                        JvmValue value; ObjectRef object_ref = 0;
                        if (is_static) {
                            // Reflective reads of static fields trigger JVM class initialization.
                            if (!initialize_class(declaring)) return {false, NullValue{}};
                            if (!statics_.get(declaring, field_name, value)) value = default_value(descriptor);
                        } else if (!as_ref(target_args[0], object_ref) || !object_ref ||
                                   !heap_.get_field(object_ref, declaring + "." + field_name, value)) {
                            return bad("IllegalArgumentException: reflected instance field receiver mismatch");
                        }
                        if (descriptor == "Z" || descriptor == "B" || descriptor == "C" || descriptor == "S" || descriptor == "I") {
                            const std::int32_t n = std::holds_alternative<std::int32_t>(value) ? std::get<std::int32_t>(value) : 0;
                            const ObjectRef boxed = heap_.allocate_object("java/lang/Integer");
                            if (!boxed || !heap_.put_field(boxed, "value", n) || !push(boxed)) return bad("Field.get boxing failed");
                        } else if (!push(value)) return bad("Field.get stack overflow");
                        continue;
                    }
                    if (target_name == "getInt" && target_desc == "(Ljava/lang/Object;)I") {
                        JvmValue value; ObjectRef object_ref = 0;
                        if (is_static) { if (!initialize_class(declaring)) return {false, NullValue{}}; if (!statics_.get(declaring, field_name, value)) value = std::int32_t(0); }
                        else if (!as_ref(target_args[0], object_ref) || !object_ref || !heap_.get_field(object_ref, declaring + "." + field_name, value)) return bad("Field.getInt receiver mismatch");
                        if (!std::holds_alternative<std::int32_t>(value) || !push(value)) return bad("Field.getInt type mismatch");
                        continue;
                    }
                    if (target_name == "set" && target_desc == "(Ljava/lang/Object;Ljava/lang/Object;)V") {
                        JvmValue value = target_args[1]; ObjectRef object_ref = 0;
                        if (descriptor == "I" && std::holds_alternative<ObjectRef>(value)) {
                            JvmValue unboxed; const ObjectRef boxed = std::get<ObjectRef>(value);
                            if (boxed && heap_.get_field(boxed, "value", unboxed)) value = unboxed;
                        }
                        if (is_static) { if (!statics_.set(declaring, field_name, value) && !statics_.define(declaring, field_name, value)) return bad("Field.set static write failed"); }
                        else if (!as_ref(target_args[0], object_ref) || !object_ref || !heap_.put_field(object_ref, declaring + "." + field_name, value)) return bad("Field.set instance write failed");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/text/SimpleDateFormat") {
                    if (target_name == "<init>" && (target_desc == "()V" || target_desc == "(Ljava/lang/String;)V" ||
                        target_desc == "(Ljava/lang/String;Ljava/util/Locale;)V")) continue;
                    if (target_name == "format" && target_desc == "(Ljava/util/Date;)Ljava/lang/String;") {
                        const ObjectRef result = heap_.allocate_string("");
                        if (!result || !push(result)) return bad("SimpleDateFormat.format allocation failed");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/io/StringWriter") {
                    auto* writer = heap_.get(target_receiver);
                    if (!writer) return bad("invalid StringWriter receiver");
                    if (target_name == "<init>" && (target_desc == "()V" || target_desc == "(I)V")) { writer->string_value.clear(); continue; }
                    if (target_name == "write" && target_desc == "(Ljava/lang/String;)V") {
                        ObjectRef text_ref = 0; std::string value;
                        if (as_ref(target_args[0], text_ref) && text_ref && heap_.get_string(text_ref, value)) writer->string_value += value;
                        continue;
                    }
                    if (target_name == "write" && target_desc == "(Ljava/lang/String;II)V") {
                        ObjectRef text_ref = 0; std::string value;
                        if (!as_ref(target_args[0], text_ref) || !text_ref || !heap_.get_string(text_ref, value)) return bad("StringWriter.write invalid string");
                        const int offset = std::get<std::int32_t>(target_args[1]), count = std::get<std::int32_t>(target_args[2]);
                        if (offset < 0 || count < 0 || static_cast<std::size_t>(offset + count) > value.size()) return bad("StringWriter.write bounds");
                        writer->string_value.append(value, static_cast<std::size_t>(offset), static_cast<std::size_t>(count));
                        continue;
                    }
                    if (target_name == "toString" && target_desc == "()Ljava/lang/String;") {
                        const ObjectRef result = heap_.allocate_string(writer->string_value);
                        if (!result || !push(result)) return bad("StringWriter.toString allocation failed");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/io/PrintWriter") {
                    if (target_name == "<init>" && (target_desc == "(Ljava/io/Writer;)V" || target_desc == "(Ljava/io/Writer;Z)V")) {
                        if (!heap_.put_field(target_receiver, "print_writer_target", target_args[0])) return bad("PrintWriter target setup failed");
                        continue;
                    }
                    if (target_name == "flush" && target_desc == "()V") continue;
                    if (target_name == "close" && target_desc == "()V") continue;
                }
                if (target_receiver && (owner == "java/lang/Throwable" || owner == "java/lang/Exception" ||
                    owner == "java/lang/RuntimeException" || owner == "java/lang/Error")) {
                    auto* throwable = heap_.get(target_receiver);
                    if (!throwable) return bad("invalid Throwable receiver");
                    if (target_name == "<init>" && target_desc == "()V") continue;
                    if (target_name == "<init>" && target_desc == "(Ljava/lang/String;)V") {
                        ObjectRef message_ref = 0;
                        std::string message;
                        if (as_ref(target_args[0], message_ref) && message_ref &&
                            heap_.get_string(message_ref, message)) {
                            throwable->string_value = message;
                            heap_.put_field(target_receiver, "detailMessage", message_ref);
                        }
                        continue;
                    }
                    if (target_name == "<init>" && target_desc == "(Ljava/lang/Throwable;)V") {
                        heap_.put_field(target_receiver, "cause", target_args[0]);
                        continue;
                    }
                    if (target_name == "getMessage" && target_desc == "()Ljava/lang/String;") {
                        JvmValue message_value = NullValue{};
                        if (heap_.get_field(target_receiver, "detailMessage", message_value)) {
                            if (!push(message_value)) return bad("Throwable.getMessage stack overflow");
                        } else if (!push(NullValue{})) {
                            return bad("Throwable.getMessage stack overflow");
                        }
                        continue;
                    }
                    if (target_name == "getLocalizedMessage" && target_desc == "()Ljava/lang/String;") {
                        JvmValue message_value = NullValue{};
                        if (!heap_.get_field(target_receiver, "detailMessage", message_value)) message_value = NullValue{};
                        if (!push(message_value)) return bad("Throwable.getLocalizedMessage stack overflow");
                        continue;
                    }
                    if (target_name == "toString" && target_desc == "()Ljava/lang/String;") {
                        std::string rendered = throwable->class_name;
                        JvmValue message_value = NullValue{};
                        if (heap_.get_field(target_receiver, "detailMessage", message_value)) {
                            ObjectRef message_ref = 0;
                            std::string message;
                            if (as_ref(message_value, message_ref) && message_ref && heap_.get_string(message_ref, message) && !message.empty())
                                rendered += ": " + message;
                        }
                        const ObjectRef rendered_ref = heap_.allocate_string(rendered);
                        if (!rendered_ref || !push(rendered_ref)) return bad("Throwable.toString stack overflow");
                        continue;
                    }
                    if (target_name == "printStackTrace" &&
                        (target_desc == "()V" || target_desc == "(Ljava/io/PrintStream;)V" || target_desc == "(Ljava/io/PrintWriter;)V")) {
                        std::ostringstream trace;
                        trace << throwable->class_name;
                        JvmValue message_value = NullValue{};
                        if (heap_.get_field(target_receiver, "detailMessage", message_value)) {
                            ObjectRef message_ref = 0;
                            std::string message;
                            if (as_ref(message_value, message_ref) && message_ref && heap_.get_string(message_ref, message) && !message.empty())
                                trace << ": " << message;
                        } else if (!throwable->string_value.empty()) {
                            trace << ": " << throwable->string_value;
                        }
                        trace << "\n";
                        const auto& frames = execution_frames_.frames();
                        for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
                            trace << "  at " << it->class_name << "." << it->method_name
                                  << it->descriptor << " (pc=" << it->pc << ")\n";
                        }
                        const std::string rendered = trace.str();
                        append_diagnostic("PRINT_STACK_TRACE\n" + rendered);
                        if (target_desc == "(Ljava/io/PrintWriter;)V" && !target_args.empty()) {
                            ObjectRef writer_ref = 0;
                            if (as_ref(target_args[0], writer_ref) && writer_ref) {
                                JvmValue target_value = NullValue{};
                                if (heap_.get_field(writer_ref, "print_writer_target", target_value)) {
                                    ObjectRef string_writer_ref = 0;
                                    if (as_ref(target_value, string_writer_ref) && string_writer_ref) {
                                        auto* string_writer = heap_.get(string_writer_ref);
                                        if (string_writer && string_writer->class_name == "java/io/StringWriter")
                                            string_writer->string_value += rendered;
                                    }
                                }
                            }
                        }
                        continue;
                    }
                }
                if (target_receiver && owner == "java/lang/Class" && target_name == "getResource" &&
                    target_desc == "(Ljava/lang/String;)Ljava/net/URL;") {
                    auto* class_object = heap_.get(target_receiver);
                    ObjectRef path_ref = 0; std::string resource_name;
                    if (!class_object || !as_ref(target_args[0], path_ref) || !path_ref ||
                        !heap_.get_string(path_ref, resource_name))
                        return bad("Class.getResource path is not a String");
                    if (!resource_name.empty() && resource_name[0] == '/') resource_name.erase(0, 1);
                    else { const auto slash = class_object->string_value.rfind('/');
                        if (slash != std::string::npos) resource_name = class_object->string_value.substr(0, slash + 1) + resource_name; }
                    std::vector<std::uint8_t> bytes;
                    if (!classes_ || !classes_->find_resource(resource_name, bytes)) {
                        if (!push(NullValue{})) return bad("Class.getResource stack overflow");
                        continue;
                    }
                    const ObjectRef url = heap_.allocate_object("java/net/URL");
                    auto* url_object = heap_.get(url);
                    if (!url || !url_object) return bad("Class.getResource URL allocation failed");
                    url_object->string_value = "resource:/" + resource_name;
                    if (!push(url)) return bad("Class.getResource stack overflow");
                    continue;
                }
                if (target_receiver && owner == "java/lang/Class" && target_name == "getResourceAsStream" &&
                    target_desc == "(Ljava/lang/String;)Ljava/io/InputStream;") {
                    auto* class_object = heap_.get(target_receiver);
                    ObjectRef path_ref = 0;
                    std::string resource_name;
                    if (!class_object || !as_ref(target_args[0], path_ref) || !path_ref ||
                        !heap_.get_string(path_ref, resource_name))
                        return bad("Class.getResourceAsStream path is not a String");
                    if (!resource_name.empty() && resource_name[0] == '/') resource_name.erase(0, 1);
                    else {
                        const auto slash = class_object->string_value.rfind('/');
                        if (slash != std::string::npos) resource_name = class_object->string_value.substr(0, slash + 1) + resource_name;
                    }
                    std::vector<std::uint8_t> resource_bytes;
                    if (!classes_ || !classes_->find_resource(resource_name, resource_bytes)) {
                        if (!push(NullValue{})) return bad("operand stack overflow");
                        continue;
                    }
                    const ObjectRef stream = heap_.allocate_byte_stream(resource_bytes);
                    if (!stream || !push(stream)) return bad("resource stream allocation failed");
                    continue;
                }
                if (target_receiver && (owner == "java/io/InputStream" || owner == "java/io/ByteArrayInputStream")) {
                    auto* stream = heap_.get(target_receiver);
                    if (!stream || (stream->stream_bytes.empty() && stream->class_name != "java/io/ByteArrayInputStream"))
                        return bad("invalid resource InputStream receiver");
                    auto read_one = [&]() -> std::int32_t {
                        if (stream->stream_position >= stream->stream_bytes.size()) return -1;
                        return static_cast<std::int32_t>(stream->stream_bytes[stream->stream_position++]);
                    };
                    if (target_name == "read" && target_desc == "()I") {
                        if (!push(read_one())) return bad("InputStream.read stack overflow");
                        continue;
                    }
                    if (target_name == "read" && target_desc == "([B)I") {
                        ObjectRef array_ref = 0;
                        if (!as_ref(target_args[0], array_ref) || !array_ref) return bad("InputStream.read byte array is null");
                        auto* array = heap_.get(array_ref);
                        if (!array || array->kind != HeapKind::PrimitiveArray || array->component_descriptor != "B") return bad("InputStream.read requires byte[]");
                        if (stream->stream_position >= stream->stream_bytes.size()) { if (!push(std::int32_t(-1))) return bad("InputStream.read stack overflow"); continue; }
                        std::size_t count = 0;
                        while (count < array->elements.size() && stream->stream_position < stream->stream_bytes.size()) {
                            if (!heap_.array_store(array_ref, count++, static_cast<std::int32_t>(stream->stream_bytes[stream->stream_position++]))) return bad("InputStream byte copy failed");
                        }
                        if (!push(static_cast<std::int32_t>(count))) return bad("InputStream.read stack overflow");
                        continue;
                    }
                    if (target_name == "read" && target_desc == "([BII)I") {
                        ObjectRef array_ref = 0; as_ref(target_args[0], array_ref);
                        const auto offset = std::get<std::int32_t>(target_args[1]);
                        const auto length = std::get<std::int32_t>(target_args[2]);
                        auto* array = heap_.get(array_ref);
                        if (!array_ref || !array || array->kind != HeapKind::PrimitiveArray || array->component_descriptor != "B" || offset < 0 || length < 0 || static_cast<std::size_t>(offset + length) > array->elements.size())
                            return bad("InputStream.read(byte[],off,len) arguments invalid");
                        if (length == 0) { if (!push(std::int32_t(0))) return bad("InputStream.read stack overflow"); continue; }
                        if (stream->stream_position >= stream->stream_bytes.size()) { if (!push(std::int32_t(-1))) return bad("InputStream.read stack overflow"); continue; }
                        std::int32_t count = 0;
                        while (count < length && stream->stream_position < stream->stream_bytes.size()) {
                            if (!heap_.array_store(array_ref, static_cast<std::size_t>(offset + count), static_cast<std::int32_t>(stream->stream_bytes[stream->stream_position++]))) return bad("InputStream byte copy failed");
                            ++count;
                        }
                        if (!push(count)) return bad("InputStream.read stack overflow");
                        continue;
                    }
                    if (target_name == "available" && target_desc == "()I") {
                        const auto remaining = stream->stream_bytes.size() - stream->stream_position;
                        if (!push(static_cast<std::int32_t>(std::min<std::size_t>(remaining, 0x7fffffff)))) return bad("InputStream.available stack overflow");
                        continue;
                    }
                    if (target_name == "close" && target_desc == "()V") { stream->stream_position = stream->stream_bytes.size(); continue; }
                }
                if (target_receiver && owner == "java/io/FileOutputStream" && target_name == "<init>" &&
                    (target_desc == "(Ljava/io/File;)V" || target_desc == "(Ljava/io/File;Z)V" ||
                     target_desc == "(Ljava/lang/String;)V" || target_desc == "(Ljava/lang/String;Z)V")) {
                    ObjectRef path_ref = 0;
                    if (!as_ref(target_args[0], path_ref) || !path_ref) return bad("FileOutputStream path is null");
                    const auto* path_object = heap_.get(path_ref);
                    auto* stream_object = heap_.get(target_receiver);
                    if (!path_object || !stream_object) return bad("FileOutputStream constructor receiver/path invalid");
                    std::string file_path;
                    if (path_object->class_name == "java/io/File" || path_object->class_name == "java/lang/String")
                        file_path = path_object->string_value;
                    else return bad("FileOutputStream path must be File or String");
                    const bool append = target_args.size() == 2 && std::holds_alternative<std::int32_t>(target_args[1]) &&
                                        std::get<std::int32_t>(target_args[1]) != 0;
                    std::ofstream output(file_path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
                    if (!output) return bad("FileOutputStream cannot open output path: " + file_path);
                    stream_object->string_value = file_path;
                    stream_object->fields["append"] = std::int32_t(1);
                    stream_object->stream_bytes.clear();
                    stream_object->stream_position = 0;
                    continue;
                }
                if (target_receiver && (owner == "java/io/FileOutputStream" || owner == "java/io/OutputStream")) {
                    auto* stream_object = heap_.get(target_receiver);
                    if (!stream_object) return bad("invalid OutputStream receiver");
                    auto write_bytes = [&](const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t length) -> bool {
                        if (offset > bytes.size() || length > bytes.size() - offset) return false;
                        std::ofstream output(stream_object->string_value, std::ios::binary | std::ios::app);
                        if (!output) return false;
                        if (length) output.write(reinterpret_cast<const char*>(bytes.data() + offset), static_cast<std::streamsize>(length));
                        return static_cast<bool>(output);
                    };
                    if (target_name == "write" && target_desc == "(I)V") {
                        const auto value = std::get<std::int32_t>(target_args[0]);
                        const std::uint8_t byte = static_cast<std::uint8_t>(value & 0xff);
                        if (!write_bytes(std::vector<std::uint8_t>{byte}, 0, 1)) return bad("OutputStream.write(int) failed");
                        continue;
                    }
                    if (target_name == "write" && (target_desc == "([B)V" || target_desc == "([BII)V")) {
                        ObjectRef array_ref = 0;
                        if (!as_ref(target_args[0], array_ref) || !array_ref) return bad("OutputStream.write byte[] is null");
                        const auto* array = heap_.get(array_ref);
                        if (!array || array->kind != HeapKind::PrimitiveArray || array->component_descriptor != "B")
                            return bad("OutputStream.write requires byte[]");
                        std::size_t offset = 0, length = array->elements.size();
                        if (target_desc == "([BII)V") {
                            const auto off = std::get<std::int32_t>(target_args[1]);
                            const auto len = std::get<std::int32_t>(target_args[2]);
                            if (off < 0 || len < 0 || static_cast<std::size_t>(off) > array->elements.size() ||
                                static_cast<std::size_t>(len) > array->elements.size() - static_cast<std::size_t>(off))
                                return bad("OutputStream.write(byte[],off,len) bounds invalid");
                            offset = static_cast<std::size_t>(off);
                            length = static_cast<std::size_t>(len);
                        }
                        std::vector<std::uint8_t> bytes;
                        bytes.reserve(array->elements.size());
                        for (const auto& element : array->elements) {
                            const auto* value = std::get_if<std::int32_t>(&element);
                            if (!value) return bad("OutputStream.write byte[] element is not numeric");
                            bytes.push_back(static_cast<std::uint8_t>(*value & 0xff));
                        }
                        if (!write_bytes(bytes, offset, length)) return bad("OutputStream.write(byte[]) failed");
                        continue;
                    }
                    if ((target_name == "flush" || target_name == "close") && target_desc == "()V") continue;
                }
                if (target_receiver && owner == "java/io/OutputStreamWriter" && target_name == "<init>" &&
                    (target_desc == "(Ljava/io/OutputStream;)V" || target_desc == "(Ljava/io/OutputStream;Ljava/lang/String;)V" ||
                     target_desc == "(Ljava/io/OutputStream;Ljava/nio/charset/Charset;)V")) {
                    ObjectRef output_ref = 0;
                    if (!as_ref(target_args[0], output_ref) || !output_ref) return bad("OutputStreamWriter requires OutputStream");
                    const auto* output = heap_.get(output_ref);
                    auto* writer = heap_.get(target_receiver);
                    if (!output || !writer || (output->class_name != "java/io/FileOutputStream" && output->class_name != "java/io/ByteArrayOutputStream"))
                        return bad("unsupported OutputStreamWriter target");
                    writer->string_value = output->string_value;
                    writer->fields["output_ref"] = output_ref;
                    continue;
                }
                if (target_receiver && (owner == "java/io/OutputStreamWriter" || owner == "java/io/BufferedWriter")) {
                    auto* writer = heap_.get(target_receiver);
                    if (!writer) return bad("invalid Writer receiver");
                    auto emit = [&](const std::string& value) -> bool {
                        ObjectRef output_ref = 0;
                        JvmValue ref_value;
                        if (heap_.get_field(target_receiver, "output_ref", ref_value)) as_ref(ref_value, output_ref);
                        const auto* output = output_ref ? heap_.get(output_ref) : nullptr;
                        const std::string& path = output && !output->string_value.empty() ? output->string_value : writer->string_value;
                        if (path.empty()) return false;
                        std::ofstream file(path, std::ios::binary | std::ios::app);
                        if (!file) return false;
                        file.write(value.data(), static_cast<std::streamsize>(value.size()));
                        return static_cast<bool>(file);
                    };
                    if (target_name == "<init>" && owner == "java/io/BufferedWriter" && target_desc == "(Ljava/io/Writer;)V") {
                        ObjectRef source_ref = 0;
                        if (!as_ref(target_args[0], source_ref) || !source_ref) return bad("BufferedWriter requires Writer");
                        const auto* source = heap_.get(source_ref);
                        if (!source || source->class_name != "java/io/OutputStreamWriter") return bad("unsupported BufferedWriter source");
                        writer->string_value = source->string_value;
                        JvmValue output_ref;
                        if (heap_.get_field(source_ref, "output_ref", output_ref)) writer->fields["output_ref"] = output_ref;
                        continue;
                    }
                    if (target_name == "write" && target_desc == "(I)V") {
                        const char ch = static_cast<char>(std::get<std::int32_t>(target_args[0]) & 0xff);
                        if (!emit(std::string(1, ch))) return bad("Writer.write(int) failed");
                        continue;
                    }
                    if (target_name == "write" && target_desc == "([CII)V") {
                        ObjectRef array_ref = 0; as_ref(target_args[0], array_ref);
                        const auto* array = heap_.get(array_ref);
                        const auto off = std::get<std::int32_t>(target_args[1]);
                        const auto len = std::get<std::int32_t>(target_args[2]);
                        if (!array || array->kind != HeapKind::PrimitiveArray || array->component_descriptor != "C" || off < 0 || len < 0 ||
                            static_cast<std::size_t>(off) > array->elements.size() || static_cast<std::size_t>(len) > array->elements.size() - static_cast<std::size_t>(off))
                            return bad("Writer.write(char[],off,len) arguments invalid");
                        std::string text; text.reserve(static_cast<std::size_t>(len));
                        for (std::int32_t i = 0; i < len; ++i) {
                            const auto* ch = std::get_if<std::int32_t>(&array->elements[static_cast<std::size_t>(off + i)]);
                            if (!ch) return bad("Writer char element is not numeric");
                            const unsigned int cp = static_cast<unsigned int>(*ch) & 0xffffu;
                            if (cp < 0x80) text.push_back(static_cast<char>(cp));
                            else if (cp < 0x800) { text.push_back(static_cast<char>(0xc0u | (cp >> 6))); text.push_back(static_cast<char>(0x80u | (cp & 0x3fu))); }
                            else { text.push_back(static_cast<char>(0xe0u | (cp >> 12))); text.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu))); text.push_back(static_cast<char>(0x80u | (cp & 0x3fu))); }
                        }
                        if (!emit(text)) return bad("Writer.write(char[]) failed");
                        continue;
                    }
                    if (target_name == "write" && (target_desc == "(Ljava/lang/String;)V" || target_desc == "(Ljava/lang/String;II)V")) {
                        ObjectRef string_ref = 0; std::string value;
                        if (!as_ref(target_args[0], string_ref) || !string_ref || !heap_.get_string(string_ref, value)) return bad("Writer.write String argument invalid");
                        std::size_t off = 0, len = value.size();
                        if (target_desc == "(Ljava/lang/String;II)V") {
                            const auto start_index = std::get<std::int32_t>(target_args[1]);
                            const auto count = std::get<std::int32_t>(target_args[2]);
                            if (start_index < 0 || count < 0 || static_cast<std::size_t>(start_index) > value.size() || static_cast<std::size_t>(count) > value.size() - static_cast<std::size_t>(start_index))
                                return bad("Writer.write(String,off,len) bounds invalid");
                            off = static_cast<std::size_t>(start_index);
                            len = static_cast<std::size_t>(count);
                        }
                        if (!emit(value.substr(off, len))) return bad("Writer.write(String) failed");
                        continue;
                    }
                    if (target_name == "newLine" && target_desc == "()V") { if (!emit("\n")) return bad("Writer.newLine failed"); continue; }
                    if ((target_name == "flush" || target_name == "close") && target_desc == "()V") continue;
                }
                if (target_receiver && (owner == "java/io/FileReader" || owner == "java/io/FileInputStream") &&
                    target_name == "<init>" && (target_desc == "(Ljava/io/File;)V" || target_desc == "(Ljava/lang/String;)V")) {
                    ObjectRef path_ref = 0;
                    if (!as_ref(target_args[0], path_ref) || !path_ref) return bad("file stream path is null");
                    const auto* path_object = heap_.get(path_ref);
                    if (!path_object) return bad("file stream path object is invalid");
                    std::string file_path;
                    if (path_object->class_name == "java/io/File") file_path = path_object->string_value;
                    else if (path_object->class_name == "java/lang/String") file_path = path_object->string_value;
                    else return bad("file stream path must be File or String");
                    auto* stream_object = heap_.get(target_receiver);
                    if (!stream_object) return bad("file stream receiver invalid");
                    stream_object->stream_bytes.clear();
                    stream_object->stream_position = 0;
                    std::ifstream input_file(file_path, std::ios::binary);
                    if (input_file) {
                        stream_object->stream_bytes.assign(std::istreambuf_iterator<char>(input_file), std::istreambuf_iterator<char>());
                    }
                    continue;
                }
                if (target_receiver && owner == "java/io/InputStreamReader" && target_name == "<init>" &&
                    (target_desc == "(Ljava/io/InputStream;)V" || target_desc == "(Ljava/io/InputStream;Ljava/lang/String;)V" ||
                     target_desc == "(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V")) {
                    ObjectRef input_ref = 0;
                    if (!as_ref(target_args[0], input_ref) || !input_ref) return bad("InputStreamReader requires InputStream");
                    const auto* input = heap_.get(input_ref);
                    auto* reader = heap_.get(target_receiver);
                    if (!input || !reader || (input->class_name != "java/io/ByteArrayInputStream" && input->class_name != "java/io/FileInputStream")) return bad("unsupported InputStreamReader source");
                    reader->stream_bytes.assign(input->stream_bytes.begin() + static_cast<std::ptrdiff_t>(input->stream_position), input->stream_bytes.end());
                    reader->stream_position = 0;
                    continue;
                }
                if (target_receiver && owner == "java/io/BufferedReader" && target_name == "<init>" &&
                    (target_desc == "(Ljava/io/Reader;)V" || target_desc == "(Ljava/io/Reader;I)V")) {
                    ObjectRef reader_ref = 0;
                    if (!as_ref(target_args[0], reader_ref) || !reader_ref) return bad("BufferedReader requires Reader");
                    const auto* input = heap_.get(reader_ref);
                    auto* reader = heap_.get(target_receiver);
                    if (!input || !reader || (input->class_name != "java/io/InputStreamReader" && input->class_name != "java/io/ByteArrayInputStream" && input->class_name != "java/io/FileReader")) return bad("unsupported BufferedReader source");
                    reader->stream_bytes.assign(input->stream_bytes.begin() + static_cast<std::ptrdiff_t>(input->stream_position), input->stream_bytes.end());
                    reader->stream_position = 0;
                    continue;
                }
                if (target_receiver && (owner == "java/io/InputStreamReader" || owner == "java/io/BufferedReader" || owner == "java/io/FileReader")) {
                    auto* reader = heap_.get(target_receiver);
                    if (!reader) return bad("invalid Reader receiver");
                    if (target_name == "read" && target_desc == "()I") {
                        const std::int32_t value = reader->stream_position < reader->stream_bytes.size() ? reader->stream_bytes[reader->stream_position++] : -1;
                        if (!push(value)) return bad("Reader.read stack overflow");
                        continue;
                    }
                    if (target_name == "read" && target_desc == "([CII)I") {
                        ObjectRef array_ref = 0; as_ref(target_args[0], array_ref);
                        const auto offset = std::get<std::int32_t>(target_args[1]);
                        const auto length = std::get<std::int32_t>(target_args[2]);
                        auto* array = heap_.get(array_ref);
                        if (!array || array->kind != HeapKind::PrimitiveArray || array->component_descriptor != "C" || offset < 0 || length < 0 || static_cast<std::size_t>(offset + length) > array->elements.size()) return bad("Reader.read(char[],off,len) arguments invalid");
                        if (length == 0) { if (!push(std::int32_t(0))) return bad("Reader.read stack overflow"); continue; }
                        if (reader->stream_position >= reader->stream_bytes.size()) { if (!push(std::int32_t(-1))) return bad("Reader.read stack overflow"); continue; }
                        std::int32_t count = 0;
                        while (count < length && reader->stream_position < reader->stream_bytes.size()) {
                            if (!heap_.array_store(array_ref, static_cast<std::size_t>(offset + count), static_cast<std::int32_t>(reader->stream_bytes[reader->stream_position++]))) return bad("Reader char copy failed");
                            ++count;
                        }
                        if (!push(count)) return bad("Reader.read stack overflow");
                        continue;
                    }
                    if (target_name == "readLine" && target_desc == "()Ljava/lang/String;") {
                        if (reader->stream_position >= reader->stream_bytes.size()) { if (!push(NullValue{})) return bad("BufferedReader.readLine stack overflow"); continue; }
                        std::string line;
                        while (reader->stream_position < reader->stream_bytes.size()) {
                            const char ch = static_cast<char>(reader->stream_bytes[reader->stream_position++]);
                            if (ch == '\n') break;
                            if (ch != '\r') line.push_back(ch);
                        }
                        const ObjectRef line_ref = heap_.allocate_string(line);
                        if (!line_ref || !push(line_ref)) return bad("BufferedReader.readLine allocation failed");
                        continue;
                    }
                    if (target_name == "ready" && target_desc == "()Z") {
                        if (!push(std::int32_t(reader->stream_position < reader->stream_bytes.size() ? 1 : 0))) return bad("Reader.ready stack overflow");
                        continue;
                    }
                    if (target_name == "close" && target_desc == "()V") { reader->stream_position = reader->stream_bytes.size(); continue; }
                }
                if (target_receiver && owner == "java/net/URL") {
                    auto* url = heap_.get(target_receiver);
                    if (!url) return bad("invalid URL receiver");
                    if (target_name == "<init>" && target_desc == "(Ljava/lang/String;)V") {
                        ObjectRef text_ref = 0; std::string external;
                        if (!as_ref(target_args[0], text_ref) || !heap_.get_string(text_ref, external))
                            return bad("URL(String) requires a string");
                        url->string_value = external;
                        continue;
                    }
                    if (target_name == "toString" && target_desc == "()Ljava/lang/String;") {
                        const ObjectRef result = heap_.allocate_string(url->string_value);
                        if (!result || !push(result)) return bad("URL.toString allocation failed");
                        continue;
                    }
                    if (target_name == "toExternalForm" && target_desc == "()Ljava/lang/String;") {
                        const ObjectRef result = heap_.allocate_string(url->string_value);
                        if (!result || !push(result)) return bad("URL.toExternalForm allocation failed");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/util/Date") {
                    auto* date = heap_.get(target_receiver);
                    if (!date) return bad("invalid Date receiver");
                    if (target_name == "<init>" && target_desc == "()V") {
                        const auto now = std::chrono::system_clock::now().time_since_epoch();
                        date->fields["time"] = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
                        continue;
                    }
                    if (target_name == "<init>" && target_desc == "(J)V") {
                        if (!std::holds_alternative<std::int64_t>(target_args[0])) return bad("Date(long) requires long");
                        date->fields["time"] = std::get<std::int64_t>(target_args[0]);
                        continue;
                    }
                    if (target_name == "getTime" && target_desc == "()J") {
                        JvmValue millis = std::int64_t(0); heap_.get_field(target_receiver, "time", millis);
                        if (!push(millis)) return bad("Date.getTime stack overflow");
                        continue;
                    }
                    if (target_name == "toString" && target_desc == "()Ljava/lang/String;") {
                        JvmValue millis = std::int64_t(0); heap_.get_field(target_receiver, "time", millis);
                        const auto value = std::holds_alternative<std::int64_t>(millis) ? std::get<std::int64_t>(millis) : 0;
                        const std::time_t seconds = static_cast<std::time_t>(value / 1000);
                        std::tm tm_value{};
#if defined(_WIN32)
                        gmtime_s(&tm_value, &seconds);
#else
                        gmtime_r(&seconds, &tm_value);
#endif
                        std::ostringstream formatted;
                        formatted << std::put_time(&tm_value, "%a %b %d %H:%M:%S UTC %Y");
                        const auto string_ref = heap_.allocate_string(formatted.str());
                        if (!string_ref || !push(string_ref)) return bad("Date.toString allocation failed");
                        continue;
                    }
                }
                if (owner == "org/lwjgl/opengl/GL11" && op == 0xb8 && target_name == "glGetString" && target_desc == "(I)Ljava/lang/String;") {
                    if (!gl_bridge_) return bad("GL11.glGetString called without a GL11 bridge");
                    if (!std::holds_alternative<std::int32_t>(target_args[0])) return bad("GL11.glGetString enum argument is not int");
                    const char* value = gl_bridge_->get_string(static_cast<std::uint32_t>(std::get<std::int32_t>(target_args[0])));
                    if (!value) { if (!push(NullValue{})) return bad("GL11.glGetString stack overflow"); continue; }
                    const ObjectRef result = heap_.allocate_string(value);
                    if (!result || !push(result)) return bad("GL11.glGetString allocation failed");
                    continue;
                }
                if (owner == "org/lwjgl/opengl/GL11" && op == 0xb8) {
                    if (target_name == "glGenLists" && target_desc == "(I)I") {
                        JvmValue next_value;
                        std::int32_t next_id = 1;
                        if (statics_.get("opti/switchport/GL11", "next_display_list", next_value) &&
                            std::holds_alternative<std::int32_t>(next_value)) next_id = std::get<std::int32_t>(next_value);
                        const auto count = std::max<std::int32_t>(0, std::get<std::int32_t>(target_args[0]));
                        if (!statics_.set("opti/switchport/GL11", "next_display_list", std::int32_t(next_id + count)))
                            statics_.define("opti/switchport/GL11", "next_display_list", std::int32_t(next_id + count));
                        if (!push(next_id)) return bad("GL11.glGenLists stack overflow");
                        continue;
                    }
                    if ((target_name == "glDeleteLists" && target_desc == "(II)V") ||
                        (target_name == "glNewList" && target_desc == "(II)V") ||
                        (target_name == "glEndList" && target_desc == "()V") ||
                        (target_name == "glCallList" && target_desc == "(I)V")) continue;
                }
                if (owner == "org/lwjgl/opengl/GL11" && op == 0xb8 && target_name == "glGetInteger" && target_desc == "(I)I") {
                    const auto pname = std::get<std::int32_t>(target_args[0]);
                    std::int32_t value = 0;
                    if (pname == 0x8069 && gl_bridge_) value = static_cast<std::int32_t>(gl_bridge_->context().texture_api().bound_2d()); // GL_TEXTURE_BINDING_2D
                    else if (gl_bridge_ && gl_bridge_->get_integer(static_cast<std::uint32_t>(pname), &value, 1)) { /* queried backend state */ }
                    else if (pname == 0x0D33) value = 2048; // GL_MAX_TEXTURE_SIZE
                    else if (pname == 0x0D31) value = 8; // GL_MAX_LIGHTS
                    else if (pname == 0x84E2) value = 1; // GL_MAX_TEXTURE_UNITS
                    else if (pname == 0x0D36) value = 32; // GL_MAX_MODELVIEW_STACK_DEPTH
                    else if (pname == 0x0D38 || pname == 0x0D39) value = 2;
                    else if (pname == 0x0D35) value = 16;
                    if (!push(value)) return bad("GL11.glGetInteger stack overflow");
                    continue;
                }
                if (owner == "org/lwjgl/opengl/GL11" && op == 0xb8 && target_name == "glGetError" && target_desc == "()I") {
                    if (!gl_bridge_) return bad("GL11.glGetError called without a GL11 bridge");
                    if (!push(static_cast<std::int32_t>(gl_bridge_->get_error()))) return bad("GL11.glGetError stack overflow");
                    continue;
                }
                if (owner == "org/lwjgl/opengl/GL11" && op == 0xb8 && gl_bridge_) {
                    auto iarg = [&](std::size_t n) -> std::int32_t { return n < target_args.size() && std::holds_alternative<std::int32_t>(target_args[n]) ? std::get<std::int32_t>(target_args[n]) : 0; };
                    auto farg = [&](std::size_t n) -> float { return n < target_args.size() && std::holds_alternative<float>(target_args[n]) ? std::get<float>(target_args[n]) : 0.0f; };
                    bool handled = true;
                    if (target_name == "glClearColor" && target_desc == "(FFFF)V") gl_bridge_->clear_color(farg(0),farg(1),farg(2),farg(3));
                    else if (target_name == "glClear" && target_desc == "(I)V") gl_bridge_->clear(static_cast<std::uint32_t>(iarg(0)));
                    else if (target_name == "glViewport" && target_desc == "(IIII)V") gl_bridge_->viewport(iarg(0),iarg(1),iarg(2),iarg(3));
                    else if (target_name == "glOrtho" && target_desc == "(DDDDDD)V" && target_args.size()==6) gl_bridge_->ortho(std::get<double>(target_args[0]),std::get<double>(target_args[1]),std::get<double>(target_args[2]),std::get<double>(target_args[3]),std::get<double>(target_args[4]),std::get<double>(target_args[5]));
                    else if (target_name == "glClearDepth" && target_desc == "(D)V") gl_bridge_->clear_depth(std::get<double>(target_args[0]));
                    else if (target_name == "glShadeModel" && target_desc == "(I)V") { /* fixed-function default is smooth */ }
                    else if (target_name == "glBindTexture" && target_desc == "(II)V") gl_bridge_->bind_texture(static_cast<std::uint32_t>(iarg(0)),static_cast<std::uint32_t>(iarg(1)));
                    else if (target_name == "glTexParameteri" && target_desc == "(III)V") gl_bridge_->tex_parameter_i(static_cast<std::uint32_t>(iarg(0)),static_cast<std::uint32_t>(iarg(1)),iarg(2));
                    else if (target_name == "glPixelStorei" && target_desc == "(II)V") gl_bridge_->pixel_store_i(static_cast<std::uint32_t>(iarg(0)),iarg(1));
                    else if (target_name == "glTexImage2D" && target_desc == "(IIIIIIIILjava/nio/ByteBuffer;)V") {
                        ObjectRef ref=0;as_ref(target_args[8],ref);const auto* buffer=heap_.get(ref);if(!buffer)return bad("glTexImage2D requires ByteBuffer");
                        JvmValue posv,limv;int pos=0,lim=static_cast<int>(buffer->elements.size());if(heap_.get_field(ref,"buffer_position",posv))pos=std::get<std::int32_t>(posv);if(heap_.get_field(ref,"buffer_limit",limv))lim=std::get<std::int32_t>(limv);
                        if(pos<0||lim<pos||static_cast<std::size_t>(lim)>buffer->elements.size())return bad("glTexImage2D invalid buffer bounds");
                        std::vector<std::uint8_t> bytes;bytes.reserve(static_cast<std::size_t>(lim-pos));for(int n=pos;n<lim;++n)bytes.push_back(static_cast<std::uint8_t>(std::get<std::int32_t>(buffer->elements[static_cast<std::size_t>(n)])));
                        gl_bridge_->tex_image_2d(static_cast<std::uint32_t>(iarg(0)),iarg(1),iarg(2),iarg(3),iarg(4),iarg(5),static_cast<std::uint32_t>(iarg(6)),static_cast<std::uint32_t>(iarg(7)),bytes.data(),bytes.size());
                    }
                    else if (target_name == "glTexSubImage2D" && target_desc == "(IIIIIIIILjava/nio/ByteBuffer;)V") {
                        ObjectRef ref=0;as_ref(target_args[8],ref);const auto* buffer=heap_.get(ref);if(!buffer)return bad("glTexSubImage2D requires ByteBuffer");
                        JvmValue posv,limv;int pos=0,lim=static_cast<int>(buffer->elements.size());if(heap_.get_field(ref,"buffer_position",posv))pos=std::get<std::int32_t>(posv);if(heap_.get_field(ref,"buffer_limit",limv))lim=std::get<std::int32_t>(limv);
                        if(pos<0||lim<pos||static_cast<std::size_t>(lim)>buffer->elements.size())return bad("glTexSubImage2D invalid buffer bounds");
                        std::vector<std::uint8_t> bytes;bytes.reserve(static_cast<std::size_t>(lim-pos));for(int n=pos;n<lim;++n)bytes.push_back(static_cast<std::uint8_t>(std::get<std::int32_t>(buffer->elements[static_cast<std::size_t>(n)])));
                        gl_bridge_->tex_sub_image_2d(static_cast<std::uint32_t>(iarg(0)),iarg(1),iarg(2),iarg(3),iarg(4),iarg(5),static_cast<std::uint32_t>(iarg(6)),static_cast<std::uint32_t>(iarg(7)),bytes.data(),bytes.size());
                    }
                    else if (target_name == "glEnable" && target_desc == "(I)V") gl_bridge_->enable(static_cast<std::uint32_t>(iarg(0)));
                    else if (target_name == "glDisable" && target_desc == "(I)V") gl_bridge_->disable(static_cast<std::uint32_t>(iarg(0)));
                    else if (target_name == "glEnableClientState" && target_desc == "(I)V") gl_bridge_->enable_client_state(static_cast<std::uint32_t>(iarg(0)));
                    else if (target_name == "glDisableClientState" && target_desc == "(I)V") gl_bridge_->disable_client_state(static_cast<std::uint32_t>(iarg(0)));
                    else if (target_name == "glDrawArrays" && target_desc == "(III)V") gl_bridge_->draw_arrays(static_cast<std::uint32_t>(iarg(0)),iarg(1),iarg(2));
                    else if (target_name == "glBlendFunc" && target_desc == "(II)V") gl_bridge_->blend_func(static_cast<std::uint32_t>(iarg(0)),static_cast<std::uint32_t>(iarg(1)));
                    else if (target_name == "glColorMask" && target_desc == "(ZZZZ)V") gl_bridge_->color_mask(iarg(0)!=0,iarg(1)!=0,iarg(2)!=0,iarg(3)!=0);
                    else if (target_name == "glDepthFunc" && target_desc == "(I)V") gl_bridge_->depth_func(static_cast<std::uint32_t>(iarg(0)));
                    else if (target_name == "glAlphaFunc" && target_desc == "(IF)V") gl_bridge_->alpha_func(static_cast<std::uint32_t>(iarg(0)),farg(1));
                    else if (target_name == "glMatrixMode" && target_desc == "(I)V") gl_bridge_->matrix_mode(static_cast<std::uint32_t>(iarg(0)));
                    else if (target_name == "glLoadIdentity" && target_desc == "()V") gl_bridge_->load_identity();
                    else if ((target_name == "glLoadMatrix" || target_name == "glMultMatrix") && target_desc == "(Ljava/nio/FloatBuffer;)V") {
                        ObjectRef ref=0; as_ref(target_args[0],ref); const auto* view=heap_.get(ref); JvmValue parent_value,pos_value,limit_value; ObjectRef parent_ref=0;
                        if(!view||view->class_name!="java/nio/FloatBuffer"||!heap_.get_field(ref,"buffer_parent",parent_value)||!as_ref(parent_value,parent_ref)||
                           !heap_.get_field(ref,"buffer_position",pos_value)||!heap_.get_field(ref,"buffer_limit",limit_value))return bad("GL matrix call requires FloatBuffer view");
                        const int position=std::get<std::int32_t>(pos_value),limit=std::get<std::int32_t>(limit_value);const auto* parent=heap_.get(parent_ref);
                        if(!parent||position<0||limit-position<16||static_cast<std::size_t>((position+16)*4)>parent->elements.size())return bad("GL matrix FloatBuffer bounds");
                        std::array<float,16> matrix{};
                        for(int n=0;n<16;++n){const std::size_t k=static_cast<std::size_t>(position+n)*4u;std::uint32_t bits=0;for(int b=0;b<4;++b)bits|=(static_cast<std::uint32_t>(std::get<std::int32_t>(parent->elements[k+static_cast<std::size_t>(b)])&0xff)<<(b*8));std::memcpy(&matrix[static_cast<std::size_t>(n)],&bits,sizeof(float));}
                        if(target_name=="glLoadMatrix")gl_bridge_->load_matrix(matrix);else gl_bridge_->mult_matrix(matrix);
                    }
                    else if (target_name == "glPushMatrix" && target_desc == "()V") gl_bridge_->push_matrix();
                    else if (target_name == "glPopMatrix" && target_desc == "()V") gl_bridge_->pop_matrix();
                    else if (target_name == "glTranslatef" && target_desc == "(FFF)V") gl_bridge_->translatef(farg(0),farg(1),farg(2));
                    else if (target_name == "glScalef" && target_desc == "(FFF)V") gl_bridge_->scalef(farg(0),farg(1),farg(2));
                    else if (target_name == "glRotatef" && target_desc == "(FFFF)V") gl_bridge_->rotatef(farg(0),farg(1),farg(2),farg(3));
                    else if (target_name == "glBegin" && target_desc == "(I)V") gl_bridge_->begin(static_cast<std::uint32_t>(iarg(0)));
                    else if (target_name == "glEnd" && target_desc == "()V") gl_bridge_->end();
                    else if (target_name == "glColor3f" && target_desc == "(FFF)V") gl_bridge_->color3f(farg(0),farg(1),farg(2));
                    else if (target_name == "glColor4f" && target_desc == "(FFFF)V") gl_bridge_->color4f(farg(0),farg(1),farg(2),farg(3));
                    else if (target_name == "glTexCoord2f" && target_desc == "(FF)V") gl_bridge_->tex_coord2f(farg(0),farg(1));
                    else if (target_name == "glVertex3f" && target_desc == "(FFF)V") gl_bridge_->vertex3f(farg(0),farg(1),farg(2));
                    else if ((target_name == "glTexCoordPointer" || target_name == "glVertexPointer" || target_name == "glColorPointer") && target_desc == "(IZILjava/nio/ByteBuffer;)V" && target_args.size()==4) {
                        ObjectRef ref=0;
                        if(!as_ref(target_args[3],ref)) return bad("GL byte client pointer requires ByteBuffer");
                        const auto* buffer=heap_.get(ref);
                        JvmValue posv,limv;
                        int pos=0,lim=0;
                        if(!buffer || buffer->class_name!="java/nio/ByteBuffer") return bad("GL byte client pointer received invalid ByteBuffer");
                        if(heap_.get_field(ref,"buffer_position",posv)) pos=std::get<std::int32_t>(posv);
                        if(heap_.get_field(ref,"buffer_limit",limv)) lim=std::get<std::int32_t>(limv);
                        if(pos<0 || lim<pos || static_cast<std::size_t>(lim)>buffer->elements.size()) return bad("GL byte client pointer buffer bounds");
                        std::vector<std::uint8_t> raw;
                        raw.reserve(static_cast<std::size_t>(lim-pos));
                        for(int n=pos;n<lim;++n) {
                            if(!std::holds_alternative<std::int32_t>(buffer->elements[static_cast<std::size_t>(n)])) return bad("GL byte client pointer contains non-byte element");
                            raw.push_back(static_cast<std::uint8_t>(std::get<std::int32_t>(buffer->elements[static_cast<std::size_t>(n)])));
                        }
                        const int components=iarg(0),stride=iarg(2);
                        const bool unsigned_bytes=iarg(1)!=0;
                        const std::uint32_t array=target_name=="glTexCoordPointer"?0x8078u:(target_name=="glVertexPointer"?0x8074u:0x8076u);
                        const opti::gl::ArrayScalar scalar=unsigned_bytes?opti::gl::ArrayScalar::UByte:opti::gl::ArrayScalar::Byte;
                        if(!gl_bridge_->set_client_array_bytes(array,components,scalar,stride,raw.data(),raw.size())) return bad("GL byte client pointer setup failed");
                    }
                    else if ((target_name == "glTexCoordPointer" || target_name == "glVertexPointer" || target_name == "glColorPointer") && target_args.size()==3 && target_desc.find("Ljava/nio/")!=std::string::npos) {
                        ObjectRef ref=0;as_ref(target_args[2],ref);const auto* view=heap_.get(ref);JvmValue parent_value,posv,limv;ObjectRef parent_ref=0;
                        if(!view||!heap_.get_field(ref,"buffer_parent",parent_value)||!as_ref(parent_value,parent_ref))return bad("client pointer requires supported NIO view");
                        const auto* parent=heap_.get(parent_ref);int pos=0,lim=0;if(heap_.get_field(ref,"buffer_position",posv))pos=std::get<std::int32_t>(posv);if(heap_.get_field(ref,"buffer_limit",limv))lim=std::get<std::int32_t>(limv);
                        const int element_size=view->class_name=="java/nio/FloatBuffer"||view->class_name=="java/nio/IntBuffer"?4:2;
                        if(!parent||pos<0||lim<pos||static_cast<std::size_t>(lim*element_size)>parent->elements.size())return bad("client pointer backing bounds");
                        std::vector<std::uint8_t> raw;raw.reserve(static_cast<std::size_t>(lim-pos)*element_size);for(int n=pos*element_size;n<lim*element_size;++n)raw.push_back(static_cast<std::uint8_t>(std::get<std::int32_t>(parent->elements[static_cast<std::size_t>(n)])));
                        const int components=iarg(0),stride=iarg(1);const std::uint32_t array=target_name=="glTexCoordPointer"?0x8078u:(target_name=="glVertexPointer"?0x8074u:0x8076u);
                        const opti::gl::ArrayScalar scalar=view->class_name=="java/nio/FloatBuffer"?opti::gl::ArrayScalar::Float:(view->class_name=="java/nio/IntBuffer"?opti::gl::ArrayScalar::Int:opti::gl::ArrayScalar::Short);
                        if(!gl_bridge_->set_client_array_bytes(array,components,scalar,stride,raw.data(),raw.size()))return bad("GL client pointer setup failed");
                    }
                    else if (target_name == "glNormalPointer" && target_args.size()==2 && target_desc.find("Ljava/nio/")!=std::string::npos) {
                        ObjectRef ref=0;as_ref(target_args[1],ref);const auto* view=heap_.get(ref);JvmValue parent_value,posv,limv;ObjectRef parent_ref=0;
                        if(!view||!heap_.get_field(ref,"buffer_parent",parent_value)||!as_ref(parent_value,parent_ref))return bad("normal pointer requires supported NIO view");
                        const auto* parent=heap_.get(parent_ref);int pos=0,lim=0;if(heap_.get_field(ref,"buffer_position",posv))pos=std::get<std::int32_t>(posv);if(heap_.get_field(ref,"buffer_limit",limv))lim=std::get<std::int32_t>(limv);const int es=view->class_name=="java/nio/FloatBuffer"?4:2;
                        if(!parent||pos<0||lim<pos||static_cast<std::size_t>(lim*es)>parent->elements.size())return bad("normal pointer backing bounds");
                        std::vector<std::uint8_t> raw;for(int n=pos*es;n<lim*es;++n)raw.push_back(static_cast<std::uint8_t>(std::get<std::int32_t>(parent->elements[static_cast<std::size_t>(n)])));
                        const opti::gl::ArrayScalar scalar=view->class_name=="java/nio/FloatBuffer"?opti::gl::ArrayScalar::Float:opti::gl::ArrayScalar::Short;if(!gl_bridge_->set_client_array_bytes(0x8075u,3,scalar,iarg(0),raw.data(),raw.size()))return bad("GL normal pointer setup failed");
                    }
                    else if (target_name == "glGenTextures" && target_desc == "(Ljava/nio/IntBuffer;)V") {
                        ObjectRef ref=0; as_ref(target_args[0],ref); auto* ib=heap_.get(ref); JvmValue parent_value; ObjectRef parent_ref=0;
                        if(!ib||!heap_.get_field(ref,"buffer_parent",parent_value)||!as_ref(parent_value,parent_ref)) return bad("glGenTextures requires supported IntBuffer");
                        auto* bb=heap_.get(parent_ref); JvmValue posv,limv; int pos=0,lim=0;
                        if(!bb||!heap_.get_field(ref,"buffer_position",posv)||!heap_.get_field(ref,"buffer_limit",limv))return bad("glGenTextures invalid IntBuffer state");
                        pos=std::get<std::int32_t>(posv);lim=std::get<std::int32_t>(limv); if(pos<0||lim<pos)return bad("glGenTextures invalid IntBuffer bounds");
                        const auto ids=gl_bridge_->gen_textures(static_cast<std::size_t>(lim-pos));
                        for(std::size_t n=0;n<ids.size();++n){const std::size_t k=(static_cast<std::size_t>(pos)+n)*4u;if(k+4>bb->elements.size())return bad("glGenTextures buffer overflow");for(int b=0;b<4;++b)bb->elements[k+b]=std::int32_t((ids[n]>>(b*8))&0xffu);}
                    }
                    else handled = false;
                    if (handled) continue;
                }
                if (owner == "org/lwjgl/opengl/GL11" && target_name.rfind("ngl", 0) == 0 &&
                    !target_desc.empty() && target_desc.back() == 'V') {
                    // Unimplemented legacy fixed-function entry points are inert in the
                    // software bridge; supported rendering calls are handled above.
                    continue;
                }
                // Native registration hooks only bind VM entry points; the host/Switch
                // runtime supplies its own implementations, so registration itself is a no-op.
                if (op == 0xb8 && target_name == "registerNatives" && target_desc == "()V" &&
                    owner.rfind("java/", 0) == 0) {
                    continue;
                }
                if (owner == "java/lang/System" && op == 0xb8 && target_name == "arraycopy" &&
                    target_desc == "(Ljava/lang/Object;ILjava/lang/Object;II)V") {
                    ObjectRef source_ref = 0, destination_ref = 0;
                    if (!as_ref(target_args[0], source_ref) || !as_ref(target_args[2], destination_ref) ||
                        !source_ref || !destination_ref) {
                        if (raise_java_exception("java/lang/NullPointerException", "System.arraycopy null array", start)) continue;
                        return {false, NullValue{}};
                    }
                    const auto source_pos = std::get<std::int32_t>(target_args[1]);
                    const auto destination_pos = std::get<std::int32_t>(target_args[3]);
                    const auto length = std::get<std::int32_t>(target_args[4]);
                    const auto* source = heap_.get(source_ref);
                    auto* destination = heap_.get(destination_ref);
                    if (!source || !destination || source->kind == HeapKind::Object || destination->kind == HeapKind::Object ||
                        source_pos < 0 || destination_pos < 0 || length < 0 ||
                        static_cast<std::size_t>(source_pos + length) > source->elements.size() ||
                        static_cast<std::size_t>(destination_pos + length) > destination->elements.size()) {
                        if (raise_java_exception("java/lang/ArrayIndexOutOfBoundsException", "System.arraycopy bounds", start)) continue;
                        return {false, NullValue{}};
                    }
                    if (source_ref == destination_ref && destination_pos > source_pos && destination_pos < source_pos + length) {
                        for (int i = length - 1; i >= 0; --i) destination->elements[static_cast<std::size_t>(destination_pos + i)] = source->elements[static_cast<std::size_t>(source_pos + i)];
                    } else {
                        for (int i = 0; i < length; ++i) destination->elements[static_cast<std::size_t>(destination_pos + i)] = source->elements[static_cast<std::size_t>(source_pos + i)];
                    }
                    continue;
                }
                if (target_receiver && owner == "java/lang/Object" && target_name == "getClass" &&
                    target_desc == "()Ljava/lang/Class;") {
                    const auto* receiver_object = heap_.get(target_receiver);
                    if (!receiver_object) return bad("Object.getClass receiver missing");
                    const ObjectRef class_ref = heap_.allocate_object("java/lang/Class");
                    auto* class_object = heap_.get(class_ref);
                    if (!class_ref || !class_object) return bad("Object.getClass Class allocation failed");
                    class_object->string_value = receiver_object->class_name;
                    if (!push(class_ref)) return bad("Object.getClass stack overflow");
                    continue;
                }
                if (owner == "java/lang/Class" && op == 0xb8 && target_name == "forName" &&
                    (target_desc == "(Ljava/lang/String;)Ljava/lang/Class;" ||
                     target_desc == "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;")) {
                    ObjectRef name_ref = 0;
                    std::string class_name;
                    if (target_args.empty() || !as_ref(target_args[0], name_ref) || !name_ref ||
                        !heap_.get_string(name_ref, class_name))
                        return bad("Class.forName requires a class-name String");
                    std::replace(class_name.begin(), class_name.end(), '.', '/');
                    if (!classes_ || !classes_->find_class(class_name))
                        return bad("ClassNotFoundException: " + class_name);
                    // Class.forName(String) initializes by default. The three-argument
                    // overload follows its explicit initialize flag.
                    const bool initialize = target_desc == "(Ljava/lang/String;)Ljava/lang/Class;" ||
                        (target_args.size() >= 2 && std::holds_alternative<std::int32_t>(target_args[1]) &&
                         std::get<std::int32_t>(target_args[1]) != 0);
                    if (initialize && !initialize_class(class_name))
                        return {false, NullValue{}};
                    const ObjectRef class_ref = heap_.allocate_object("java/lang/Class");
                    auto* class_object = heap_.get(class_ref);
                    if (!class_ref || !class_object) return bad("Class.forName allocation failed");
                    class_object->string_value = class_name;
                    if (!push(class_ref)) return bad("Class.forName stack overflow");
                    continue;
                }
                if (owner == "java/security/AccessController" && op == 0xb8 && target_name == "doPrivileged" &&
                    (target_desc == "(Ljava/security/PrivilegedAction;)Ljava/lang/Object;" ||
                     target_desc == "(Ljava/security/PrivilegedExceptionAction;)Ljava/lang/Object;" ||
                     target_desc == "(Ljava/security/PrivilegedAction;Ljava/security/AccessControlContext;)Ljava/lang/Object;" ||
                     target_desc == "(Ljava/security/PrivilegedExceptionAction;Ljava/security/AccessControlContext;)Ljava/lang/Object;")) {
                    ObjectRef action_ref = 0;
                    if (target_args.empty() || !as_ref(target_args[0], action_ref) || !action_ref)
                        return bad("AccessController.doPrivileged requires an action object");
                    const auto* action_object = heap_.get(action_ref);
                    if (!action_object) return bad("AccessController.doPrivileged action is invalid");
                    if (action_object->class_name == "org/lwjgl/input/OpenGLPackageAccess$2") {
                        const ObjectRef implementation = heap_.allocate_object("org/lwjgl/opengl/DisplayImplementation");
                        if (!implementation || !push(implementation)) return bad("LWJGL host input implementation allocation failed");
                        continue;
                    }
                    auto action_method = resolve_method(action_object->class_name, "run", "()Ljava/lang/Object;",
                                                        InvokeKind::Virtual, action_object->class_name);
                    if (!action_method.method || !action_method.owner)
                        return bad("AccessController.doPrivileged action has no run() method: " + action_object->class_name);
                    auto action_result = execute(*action_method.owner, *action_method.method, {}, action_ref, depth + 1);
                    if (!action_result.first) return {false, NullValue{}};
                    if (!push(action_result.second)) return bad("AccessController.doPrivileged stack overflow");
                    continue;
                }
                if (owner == "java/lang/Thread" && target_name == "currentThread" && target_desc == "()Ljava/lang/Thread;" && op == 0xb8) {
                    if (!current_thread_) current_thread_ = heap_.allocate_object("java/lang/Thread");
                    if (!current_thread_ || !push(current_thread_)) return bad("Thread.currentThread allocation failed");
                    continue;
                }
                if (owner == "java/lang/Thread" && target_name == "interrupted" && target_desc == "()Z" && op == 0xb8) {
                    JvmValue flag = std::int32_t(0);
                    if (current_thread_) heap_.get_field(current_thread_, "thread_interrupted", flag);
                    if (current_thread_) heap_.put_field(current_thread_, "thread_interrupted", std::int32_t(0));
                    if (!push(flag)) return bad("Thread.interrupted stack overflow");
                    continue;
                }
                if (owner == "java/lang/Thread" && target_name == "sleep" && op == 0xb8 && (target_desc == "(J)V" || target_desc == "(JI)V")) continue;
                if (target_receiver && (owner == "java/lang/Thread" || (classes_ && classes_->is_assignable(receiver_class, "java/lang/Thread")))) {
                    auto* thread = heap_.get(target_receiver);
                    if (!thread) return bad("invalid Thread receiver");
                    if (target_name == "<init>" && (target_desc == "()V" || target_desc == "(Ljava/lang/String;)V" ||
                        target_desc == "(Ljava/lang/Runnable;)V" || target_desc == "(Ljava/lang/Runnable;Ljava/lang/String;)V")) {
                        if (target_desc.find("String") != std::string::npos) {
                            const std::size_t index = target_desc == "(Ljava/lang/String;)V" ? 0u : 1u;
                            ObjectRef name_ref = 0; std::string name;
                            if (as_ref(target_args[index], name_ref) && name_ref && heap_.get_string(name_ref, name)) thread->string_value = name;
                        }
                        if (target_desc.find("Runnable") != std::string::npos) {
                            const std::size_t index = target_desc == "(Ljava/lang/Runnable;)V" ? 0u : 0u;
                            ObjectRef runnable_ref = 0;
                            if (as_ref(target_args[index], runnable_ref) && runnable_ref)
                                heap_.put_field(target_receiver, "thread_runnable", runnable_ref);
                        }
                        heap_.put_field(target_receiver, "thread_started", std::int32_t(0));
                        heap_.put_field(target_receiver, "thread_alive", std::int32_t(0));
                        continue;
                    }
                    if (target_name == "start" && target_desc == "()V") {
                        JvmValue started_value;
                        if (heap_.get_field(target_receiver, "thread_started", started_value) &&
                            std::holds_alternative<std::int32_t>(started_value) && std::get<std::int32_t>(started_value) != 0)
                            return bad("IllegalThreadStateException: thread already started");
                        heap_.put_field(target_receiver, "thread_started", std::int32_t(1));
                        // Run only Minecraft's named foreground game thread. Background
                        // workers (including the OptiFine version checker) are deferred
                        // until a real Java thread scheduler is available, so network I/O
                        // cannot block the first-frame path.
                        if (thread->string_value != "Minecraft main thread") {
                            heap_.put_field(target_receiver, "thread_alive", std::int32_t(0));
                            heap_.put_field(target_receiver, "thread_deferred", std::int32_t(1));
                            continue;
                        }
                        heap_.put_field(target_receiver, "thread_alive", std::int32_t(1));
                        ObjectRef runnable_ref = 0;
                        JvmValue runnable_value;
                        if (heap_.get_field(target_receiver, "thread_runnable", runnable_value))
                            as_ref(runnable_value, runnable_ref);
                        ObjectRef run_receiver = runnable_ref ? runnable_ref : target_receiver;
                        const auto* run_object = heap_.get(run_receiver);
                        if (classes_ && run_object) {
                            const std::string run_owner = runnable_ref ? run_object->class_name : "java/lang/Thread";
                            auto run_method = resolve_method(run_owner, "run", "()V", InvokeKind::Virtual, run_object->class_name);
                            if (run_method.method && run_method.owner) {
                                const ObjectRef previous_thread = current_thread_;
                                current_thread_ = target_receiver;
                                auto run_result = execute(*run_method.owner, *run_method.method, {}, run_receiver, depth + 1);
                                current_thread_ = previous_thread;
                                if (!run_result.first) {
                                    heap_.put_field(target_receiver, "thread_alive", std::int32_t(0));
                                    return {false, NullValue{}};
                                }
                            }
                        }
                        heap_.put_field(target_receiver, "thread_alive", std::int32_t(0));
                        continue;
                    }
                    if (target_name == "join" && (target_desc == "()V" || target_desc == "(J)V" || target_desc == "(JI)V")) continue;
                    if (target_name == "interrupt" && target_desc == "()V") { heap_.put_field(target_receiver, "thread_interrupted", std::int32_t(1)); continue; }
                    if (target_name == "isInterrupted" && target_desc == "()Z") { JvmValue v; if (!heap_.get_field(target_receiver, "thread_interrupted", v)) v = std::int32_t(0); if (!push(v)) return bad("Thread.isInterrupted stack overflow"); continue; }
                    if (target_name == "setDaemon" && target_desc == "(Z)V") { heap_.put_field(target_receiver, "thread_daemon", target_args[0]); continue; }
                    if (target_name == "isDaemon" && target_desc == "()Z") { JvmValue v; if (!heap_.get_field(target_receiver, "thread_daemon", v)) v = std::int32_t(0); if (!push(v)) return bad("Thread.isDaemon stack overflow"); continue; }
                    if (target_name == "isAlive" && target_desc == "()Z") { JvmValue v; if (!heap_.get_field(target_receiver, "thread_alive", v)) v = std::int32_t(0); if (!push(v)) return bad("Thread.isAlive stack overflow"); continue; }
                    if (target_name == "getName" && target_desc == "()Ljava/lang/String;") { const ObjectRef name = heap_.allocate_string(thread->string_value); if (!name || !push(name)) return bad("Thread.getName allocation failed"); continue; }
                    if (target_name == "setName" && target_desc == "(Ljava/lang/String;)V") { ObjectRef name_ref=0; if (!as_ref(target_args[0],name_ref)||!heap_.get_string(name_ref,thread->string_value)) return bad("Thread.setName requires String"); continue; }
                    if (target_name == "setPriority" && target_desc == "(I)V") { heap_.put_field(target_receiver, "thread_priority", target_args[0]); continue; }
                    if (target_name == "getPriority" && target_desc == "()I") { JvmValue priority; if (!heap_.get_field(target_receiver, "thread_priority", priority)) priority = std::int32_t(5); if (!push(priority)) return bad("Thread.getPriority stack overflow"); continue; }
                }
                if (target_receiver && owner == "java/io/PrintStream") {
                    if (target_name == "<init>" && (target_desc == "()V" || target_desc == "(Ljava/io/OutputStream;)V")) continue;
                    if ((target_name == "print" || target_name == "println") &&
                        (target_desc == "()V" || target_desc == "(Ljava/lang/String;)V" || target_desc == "(Ljava/lang/Object;)V" ||
                         target_desc == "(I)V" || target_desc == "(J)V" || target_desc == "(Z)V" || target_desc == "(C)V" ||
                         target_desc == "(F)V" || target_desc == "(D)V")) continue;
                    if (target_name == "flush" && target_desc == "()V") continue;
                    if (target_name == "close" && target_desc == "()V") continue;
                    if (target_name == "checkError" && target_desc == "()Z") { if (!push(std::int32_t(0))) return bad("PrintStream.checkError stack overflow"); continue; }
                }
                const auto* receiver_definition = classes_ ? classes_->find_class(receiver_class) : nullptr;
                const bool enum_receiver = owner == "java/lang/Enum" ||
                    (receiver_definition && receiver_definition->super_class == "java/lang/Enum");
                if (target_receiver && enum_receiver) {
                    auto* enum_object = heap_.get(target_receiver);
                    if (!enum_object) return bad("invalid Enum receiver");
                    if (target_name == "<init>" && target_desc == "(Ljava/lang/String;I)V") {
                        ObjectRef name_ref = 0;
                        std::string enum_name;
                        if (!as_ref(target_args[0], name_ref) || !heap_.get_string(name_ref, enum_name))
                            return bad("Enum constructor name is not a String");
                        enum_object->string_value = enum_name;
                        heap_.put_field(target_receiver, "enum_ordinal", target_args[1]);
                        continue;
                    }
                    if (target_name == "name" && target_desc == "()Ljava/lang/String;") {
                        const auto name_ref = heap_.allocate_string(enum_object->string_value);
                        if (!name_ref || !push(name_ref)) return bad("Enum.name allocation failed");
                        continue;
                    }
                    if (target_name == "ordinal" && target_desc == "()I") {
                        JvmValue ordinal;
                        if (!heap_.get_field(target_receiver, "enum_ordinal", ordinal)) ordinal = std::int32_t(0);
                        if (!push(ordinal)) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "toString" && target_desc == "()Ljava/lang/String;") {
                        const auto name_ref = heap_.allocate_string(enum_object->string_value);
                        if (!name_ref || !push(name_ref)) return bad("Enum.toString allocation failed");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/lang/Object" &&
                    target_name == "<init>" && target_desc == "()V") continue;

                // AWT compatibility layer. These Java classes are represented by
                // heap objects and forwarded to an optional native window host.
                // It deliberately implements only the exact legacy AWT surface
                // used during Minecraft 1.2.5 startup; it is not a general AWT.
                auto field_int = [&](ObjectRef object_ref, const std::string& key, int fallback = 0) {
                    JvmValue value;
                    if (!heap_.get_field(object_ref, key, value)) return fallback;
                    if (const auto* number = std::get_if<std::int32_t>(&value)) return static_cast<int>(*number);
                    return fallback;
                };
                auto field_ref = [&](ObjectRef object_ref, const std::string& key) {
                    JvmValue value;
                    ObjectRef result = 0;
                    if (heap_.get_field(object_ref, key, value)) as_ref(value, result);
                    return result;
                };
                auto store_ref = [&](ObjectRef object_ref, const std::string& key, ObjectRef value) {
                    return heap_.put_field(object_ref, key, value);
                };
                auto store_int = [&](ObjectRef object_ref, const std::string& key, int value) {
                    return heap_.put_field(object_ref, key, static_cast<std::int32_t>(value));
                };
                if (target_receiver && owner.rfind("java/awt/event/", 0) == 0 && target_name == "<init>" && target_desc == "()V") continue;
                if (target_receiver && (owner == "java/awt/Panel" || owner == "java/awt/Component") &&
                    target_name == "<init>" && (target_desc == "()V" || target_desc == "(Ljava/awt/LayoutManager;)V")) continue;
                if (target_receiver && owner == "java/awt/Container") {
                    auto* container = heap_.get(target_receiver);
                    if (!container) return bad("invalid AWT Container receiver");
                    if (target_name == "removeAll" && target_desc == "()V") {
                        container->fields.erase("content");
                        container->fields.erase("components");
                        continue;
                    }
                    if (target_name == "setLayout" && target_desc == "(Ljava/awt/LayoutManager;)V") continue;
                    if (target_name == "add" && target_desc == "(Ljava/awt/Component;Ljava/lang/Object;)V") {
                        ObjectRef child = 0;
                        if (!as_ref(target_args[0], child) || !child || !heap_.put_field(target_receiver, "content", child))
                            return bad("AWT Container.add invalid component");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/awt/Frame") {
                    auto* frame = heap_.get(target_receiver);
                    if (!frame) return bad("invalid AWT Frame receiver");
                    if (target_name == "<init>" && target_desc == "(Ljava/lang/String;)V") {
                        ObjectRef title_ref = 0; std::string title;
                        if (!as_ref(target_args[0], title_ref) || (title_ref && !heap_.get_string(title_ref, title)))
                            return bad("AWT Frame title is not a Java String");
                        frame->string_value = title;
                        continue;
                    }
                    if (target_name == "removeAll" && target_desc == "()V") {
                        frame->fields.erase("content");
                        frame->fields.erase("components");
                        continue;
                    }
                    if (target_name == "setLayout" && target_desc == "(Ljava/awt/LayoutManager;)V") continue;
                    if (target_name == "add" && target_desc == "(Ljava/awt/Component;Ljava/lang/Object;)V") {
                        ObjectRef child = 0;
                        if (!as_ref(target_args[0], child) || !child || !store_ref(target_receiver, "content", child))
                            return bad("AWT Frame.add invalid component");
                        continue;
                    }
                    if (target_name == "pack" && target_desc == "()V") {
                        const ObjectRef canvas = field_ref(target_receiver, "content");
                        const ObjectRef dimension = field_ref(canvas, "preferredSize");
                        const int width = field_int(dimension, "width", 854);
                        const int height = field_int(dimension, "height", 480);
                        if (width <= 0 || height <= 0) return bad("AWT Frame.pack invalid preferred size");
                        store_int(target_receiver, "width", width);
                        store_int(target_receiver, "height", height);
                        if (awt_host_ && awt_host_->create_window &&
                            !awt_host_->create_window(awt_host_->user, frame->string_value.c_str(), width, height))
                            return bad("AWT host rejected window creation");
                        continue;
                    }
                    if (target_name == "setLocationRelativeTo" && target_desc == "(Ljava/awt/Component;)V") continue;
                    if (target_name == "setVisible" && target_desc == "(Z)V") {
                        const bool visible = std::get<std::int32_t>(target_args[0]) != 0;
                        store_int(target_receiver, "visible", visible ? 1 : 0);
                        if (awt_host_ && awt_host_->set_visible &&
                            !awt_host_->set_visible(awt_host_->user, visible))
                            return bad("AWT host rejected visibility change");
                        continue;
                    }
                    if (target_name == "addWindowListener" && target_desc == "(Ljava/awt/event/WindowListener;)V") continue;
                    if ((target_name == "validate" || target_name == "invalidate" || target_name == "setResizable") &&
                        (target_desc == "()V" || target_desc == "(Z)V")) continue;
                }
                if (target_receiver && owner == "java/awt/Canvas") {
                    if (target_name == "<init>" && target_desc == "()V") continue;
                    if (target_name == "setPreferredSize" && target_desc == "(Ljava/awt/Dimension;)V") {
                        ObjectRef dimension = 0;
                        if (!as_ref(target_args[0], dimension) || !dimension || !store_ref(target_receiver, "preferredSize", dimension))
                            return bad("AWT Canvas preferred size is invalid");
                        continue;
                    }
                    if (target_name == "getGraphics" && target_desc == "()Ljava/awt/Graphics;") {
                        const ObjectRef graphics = heap_.allocate_object("java/awt/Graphics");
                        if (!graphics || !push(graphics)) return bad("AWT Graphics allocation failed");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/awt/Font") {
                    auto* font = heap_.get(target_receiver);
                    if (!font) return bad("invalid AWT Font receiver");
                    if (target_name == "<init>" && target_desc == "(Ljava/lang/String;II)V") {
                        ObjectRef name_ref = 0; std::string name;
                        if (as_ref(target_args[0], name_ref) && name_ref) heap_.get_string(name_ref, name);
                        font->string_value = name;
                        heap_.put_field(target_receiver, "font_style", target_args[1]);
                        heap_.put_field(target_receiver, "font_size", target_args[2]);
                        continue;
                    }
                    if (target_name == "getSize" && target_desc == "()I") {
                        JvmValue size; if (!heap_.get_field(target_receiver, "font_size", size)) size = std::int32_t(12);
                        if (!push(size)) return bad("Font.getSize stack overflow");
                        continue;
                    }
                    if (target_name == "getStyle" && target_desc == "()I") {
                        JvmValue style; if (!heap_.get_field(target_receiver, "font_style", style)) style = std::int32_t(0);
                        if (!push(style)) return bad("Font.getStyle stack overflow");
                        continue;
                    }
                    if (target_name == "getName" && target_desc == "()Ljava/lang/String;") {
                        const ObjectRef name = heap_.allocate_string(font->string_value);
                        if (!name || !push(name)) return bad("Font.getName allocation failed");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/awt/TextArea") {
                    if (target_name == "<init>" && (target_desc == "()V" || target_desc == "(Ljava/lang/String;)V" ||
                        target_desc == "(Ljava/lang/String;III)V")) {
                        std::string text; ObjectRef text_ref = 0;
                        if (!target_args.empty() && as_ref(target_args[0], text_ref) && text_ref) heap_.get_string(text_ref, text);
                        heap_.get(target_receiver)->string_value = text;
                        continue;
                    }
                    if (target_name == "setFont" && target_desc == "(Ljava/awt/Font;)V") { heap_.put_field(target_receiver, "font", target_args[0]); continue; }
                    if (target_name == "setEditable" && target_desc == "(Z)V") { heap_.put_field(target_receiver, "editable", target_args[0]); continue; }
                    if (target_name == "append" && target_desc == "(Ljava/lang/String;)V") {
                        ObjectRef text_ref = 0; std::string text;
                        if (as_ref(target_args[0], text_ref) && text_ref) heap_.get_string(text_ref, text);
                        heap_.get(target_receiver)->string_value += text;
                        continue;
                    }
                    if (target_name == "setText" && target_desc == "(Ljava/lang/String;)V") {
                        ObjectRef text_ref = 0; std::string text;
                        if (as_ref(target_args[0], text_ref) && text_ref) heap_.get_string(text_ref, text);
                        heap_.get(target_receiver)->string_value = text;
                        continue;
                    }
                    if (target_name == "getText" && target_desc == "()Ljava/lang/String;") {
                        const ObjectRef result = heap_.allocate_string(heap_.get(target_receiver)->string_value);
                        if (!result || !push(result)) return bad("TextArea.getText allocation failed");
                        continue;
                    }
                }
                bool owner_is_awt_component = false;
                if (classes_) {
                    std::string current_owner = owner;
                    std::unordered_set<std::string> awt_seen;
                    while (!current_owner.empty() && awt_seen.insert(current_owner).second) {
                        if (current_owner == "java/awt/Component") { owner_is_awt_component = true; break; }
                        const auto* owner_class = classes_->find_class(current_owner);
                        if (!owner_class) break;
                        current_owner = owner_class->super_class;
                    }
                }
                if (target_receiver && classes_ && (owner_is_awt_component || owner == "uu" || owner == "eo" || owner == "aku" ||
                    classes_->is_assignable(receiver_class, "java/awt/Component"))) {
                    if ((target_name == "setBackground" || target_name == "setForeground") && target_desc == "(Ljava/awt/Color;)V") continue;
                    if (target_name == "setLayout" && target_desc == "(Ljava/awt/LayoutManager;)V") continue;
                    if (target_name == "add" && (target_desc == "(Ljava/awt/Component;)Ljava/awt/Component;" ||
                        target_desc == "(Ljava/awt/Component;Ljava/lang/Object;)V")) {
                        ObjectRef child = 0;
                        if (!as_ref(target_args[0], child) || !child) return bad("AWT Component.add invalid child");
                        auto* parent = heap_.get(target_receiver);
                        parent->elements.push_back(child);
                        if (target_desc == "(Ljava/awt/Component;)Ljava/awt/Component;") {
                            if (!push(child)) return bad("AWT Component.add stack overflow");
                        }
                        continue;
                    }
                    if (target_name == "removeAll" && target_desc == "()V") { heap_.get(target_receiver)->elements.clear(); continue; }
                    if (target_name == "setSize" && target_desc == "(II)V") {
                        heap_.put_field(target_receiver, "width", target_args[0]);
                        heap_.put_field(target_receiver, "height", target_args[1]);
                        continue;
                    }
                    if ((target_name == "setPreferredSize" || target_name == "setMinimumSize" || target_name == "setMaximumSize") && target_desc == "(Ljava/awt/Dimension;)V") {
                        heap_.put_field(target_receiver, target_name == "setPreferredSize" ? "preferredSize" : (target_name == "setMinimumSize" ? "minimumSize" : "maximumSize"), target_args[0]);
                        continue;
                    }
                    if (target_name == "setVisible" && target_desc == "(Z)V") continue;
                    if (target_name == "setEnabled" && target_desc == "(Z)V") continue;
                    if (target_name == "setFocusable" && target_desc == "(Z)V") continue;
                    if (target_name == "requestFocus" && target_desc == "()V") continue;
                    if (target_name == "repaint" && (target_desc == "()V" || target_desc == "(IIII)V")) continue;
                }
                if (target_receiver && owner == "java/awt/Color") {
                    if (target_name == "<init>" && target_desc == "(I)V") {
                        heap_.put_field(target_receiver, "awt_color_rgb", target_args[0]);
                        continue;
                    }
                    if (target_name == "<init>" && (target_desc == "(III)V" || target_desc == "(IIII)V")) {
                        const int r = std::get<std::int32_t>(target_args[0]) & 255;
                        const int g = std::get<std::int32_t>(target_args[1]) & 255;
                        const int b = std::get<std::int32_t>(target_args[2]) & 255;
                        const int a = target_args.size() == 4 ? (std::get<std::int32_t>(target_args[3]) & 255) : 255;
                        heap_.put_field(target_receiver, "awt_color_rgb", std::int32_t((a << 24) | (r << 16) | (g << 8) | b));
                        continue;
                    }
                    if (target_name == "getRGB" && target_desc == "()I") {
                        JvmValue rgb; if (!heap_.get_field(target_receiver, "awt_color_rgb", rgb)) rgb = std::int32_t(0xff000000u);
                        if (!push(rgb)) return bad("Color.getRGB stack overflow");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/awt/Graphics") {
                    if (target_name == "setColor" && target_desc == "(Ljava/awt/Color;)V") continue;
                    if (target_name == "fillRect" && target_desc == "(IIII)V") continue;
                    if (target_name == "dispose" && target_desc == "()V") continue;
                    if (target_name == "setFont" && target_desc == "(Ljava/awt/Font;)V") continue;
                    if (target_name == "drawString" && target_desc == "(Ljava/lang/String;II)V") continue;
                }
                if (target_receiver && owner == "org/lwjgl/opengl/DisplayMode") {
                    if (target_name == "<init>" && target_desc == "(II)V") {
                        if (!store_int(target_receiver, "width", std::get<std::int32_t>(target_args[0])) ||
                            !store_int(target_receiver, "height", std::get<std::int32_t>(target_args[1])))
                            return bad("LWJGL DisplayMode initialization failed");
                        continue;
                    }
                    if (target_name == "getWidth" && target_desc == "()I") {
                        if (!push(std::int32_t(field_int(target_receiver, "width", 854)))) return bad("DisplayMode.getWidth stack overflow");
                        continue;
                    }
                    if (target_name == "getHeight" && target_desc == "()I") {
                        if (!push(std::int32_t(field_int(target_receiver, "height", 480)))) return bad("DisplayMode.getHeight stack overflow");
                        continue;
                    }
                    if (target_name == "getBitsPerPixel" && target_desc == "()I") { if(!push(std::int32_t(field_int(target_receiver, "bpp", 32))))return bad("DisplayMode bpp stack overflow"); continue; }
                    if (target_name == "getFrequency" && target_desc == "()I") { if(!push(std::int32_t(field_int(target_receiver, "freq", 60))))return bad("DisplayMode frequency stack overflow"); continue; }
                    if (target_name == "isFullscreenCapable" && target_desc == "()Z") { if(!push(std::int32_t(1)))return bad("DisplayMode fullscreen stack overflow"); continue; }
                    if (target_name == "equals" && target_desc == "(Ljava/lang/Object;)Z") {
                        ObjectRef other_ref=0; as_ref(target_args[0],other_ref); const auto* other=other_ref?heap_.get(other_ref):nullptr;
                        const bool equal=other&&other->class_name=="org/lwjgl/opengl/DisplayMode"&&
                            field_int(target_receiver,"width",854)==field_int(other_ref,"width",854)&&
                            field_int(target_receiver,"height",480)==field_int(other_ref,"height",480)&&
                            field_int(target_receiver,"bpp",32)==field_int(other_ref,"bpp",32)&&
                            field_int(target_receiver,"freq",60)==field_int(other_ref,"freq",60);
                        if (!push(std::int32_t(equal ? 1 : 0))) return bad("DisplayMode.equals stack overflow");
                        continue;
                    }
                    if (target_name == "hashCode" && target_desc == "()I") { const int hash=field_int(target_receiver,"width",854)^field_int(target_receiver,"height",480)^field_int(target_receiver,"freq",60); if(!push(std::int32_t(hash)))return bad("DisplayMode.hashCode stack overflow"); continue; }
                }
                if (target_receiver && owner == "org/lwjgl/opengl/PixelFormat") {
                    if (target_name == "<init>" && (target_desc == "()V" || target_desc == "(IIII)V" || target_desc == "(IIIII)V")) continue;
                    if (target_name.rfind("with", 0) == 0 && target_return == "Lorg/lwjgl/opengl/PixelFormat;") {
                        if (!push(target_receiver)) return bad("PixelFormat.with* stack overflow");
                        continue;
                    }
                }
                if (owner == "org/lwjgl/Sys" && target_name == "getVersion" && target_desc == "()Ljava/lang/String;" && op == 0xb8) {
                    const ObjectRef version = heap_.allocate_string("2.9.0-switchport");
                    if (!version || !push(version)) return bad("LWJGL version string allocation failed");
                    continue;
                }
                if (owner == "org/lwjgl/opengl/GLContext" && target_name == "getCapabilities" &&
                    target_desc == "()Lorg/lwjgl/opengl/ContextCapabilities;" && op == 0xb8) {
                    ObjectRef caps = 0;
                    JvmValue cached;
                    if (statics_.get("opti/switchport/GLContext", "capabilities", cached)) as_ref(cached, caps);
                    if (!caps) {
                        caps = heap_.allocate_object("org/lwjgl/opengl/ContextCapabilities");
                        if (!caps || !heap_.put_field(caps, "GL_ARB_multitexture", std::int32_t(0)) ||
                            !heap_.put_field(caps, "OpenGL13", std::int32_t(0)) ||
                            !statics_.define("opti/switchport/GLContext", "capabilities", caps))
                            return bad("LWJGL capabilities allocation failed");
                    }
                    if (!push(caps)) return bad("GLContext.getCapabilities stack overflow");
                    continue;
                }
                if (target_receiver && owner == "java/io/PrintWriter" && target_name == "<init>") {
                    if (!heap_.put_field(target_receiver, "java/io/Writer.lock", target_receiver))
                        return bad("PrintWriter lock initialization failed");
                    continue;
                }
                if (target_receiver && owner == "sd" && !target_desc.empty() && target_desc.back() == 'V') {
                    // SoundSystem's desktop OpenAL/JOrbis backend is replaced later by
                    // the Switch audio adapter; do not initialize desktop audio here.
                    continue;
                }
                if (target_receiver && owner == "java/io/Writer" && target_name == "<init>") {
                    ObjectRef lock_ref = target_receiver;
                    if (target_desc == "(Ljava/lang/Object;)V") {
                        if (target_args.empty() || !as_ref(target_args[0], lock_ref) || !lock_ref)
                            return bad("Writer(Object) requires a non-null lock");
                    } else if (target_desc != "()V") {
                        // Other Writer constructor forms are not part of the current client path.
                        return bad("unsupported Writer constructor: " + target_desc);
                    }
                    if (!heap_.put_field(target_receiver, "java/io/Writer.lock", lock_ref)) return bad("Writer lock initialization failed");
                    continue;
                }
                if (target_receiver && owner == "java/lang/Runtime") {
                    if (target_name == "totalMemory" && target_desc == "()J") { if (!push(std::int64_t(512LL * 1024LL * 1024LL))) return bad("Runtime.totalMemory stack overflow"); continue; }
                    if (target_name == "freeMemory" && target_desc == "()J") { if (!push(std::int64_t(256LL * 1024LL * 1024LL))) return bad("Runtime.freeMemory stack overflow"); continue; }
                    if (target_name == "maxMemory" && target_desc == "()J") { if (!push(std::int64_t(512LL * 1024LL * 1024LL))) return bad("Runtime.maxMemory stack overflow"); continue; }
                    if (target_name == "availableProcessors" && target_desc == "()I") { if (!push(std::int32_t(4))) return bad("Runtime.availableProcessors stack overflow"); continue; }
                    if ((target_name == "gc" || target_name == "runFinalization" || target_name == "traceInstructions" || target_name == "traceMethodCalls") && target_desc == "()V") continue;
                }
                if (owner == "java/util/TimeZone" && op == 0xb8) {
                    if (target_name == "getSystemTimeZoneID" && target_desc == "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;") {
                        const ObjectRef zone = heap_.allocate_string("UTC");
                        if (!zone || !push(zone)) return bad("TimeZone.getSystemTimeZoneID stack overflow");
                        continue;
                    }
                    if (target_name == "getSystemGMTOffsetID" && target_desc == "()Ljava/lang/String;") {
                        const ObjectRef zone = heap_.allocate_string("GMT+00:00");
                        if (!zone || !push(zone)) return bad("TimeZone.getSystemGMTOffsetID stack overflow");
                        continue;
                    }
                    if (target_name == "getTimeZone" &&
                        (target_desc == "(Ljava/lang/String;)Ljava/util/TimeZone;" ||
                         target_desc == "(Ljava/lang/String;Z)Ljava/util/TimeZone;")) {
                        ObjectRef id_ref = 0; std::string id = "UTC";
                        if (!target_args.empty() && as_ref(target_args[0], id_ref) && id_ref) heap_.get_string(id_ref, id);
                        const ObjectRef zone = heap_.allocate_object("java/util/TimeZone");
                        if (!zone || !heap_.put_field(zone, "time_zone_id", heap_.allocate_string(id)) || !push(zone))
                            return bad("TimeZone.getTimeZone allocation failed");
                        continue;
                    }
                }
                if (target_receiver && owner == "java/util/TimeZone") {
                    if (target_name == "getRawOffset" && target_desc == "()I") { if (!push(std::int32_t(0))) return bad("TimeZone.getRawOffset stack overflow"); continue; }
                    if (target_name == "getOffset" && (target_desc == "(J)I" || target_desc == "(IIIIII)I")) { if (!push(std::int32_t(0))) return bad("TimeZone.getOffset stack overflow"); continue; }
                    if ((target_name == "useDaylightTime" || target_name == "inDaylightTime") && target_desc != "()Z") { if (!push(std::int32_t(0))) return bad("TimeZone daylight stack overflow"); continue; }
                    if (target_name == "useDaylightTime" && target_desc == "()Z") { if (!push(std::int32_t(0))) return bad("TimeZone daylight stack overflow"); continue; }
                    if (target_name == "getDSTSavings" && target_desc == "()I") { if (!push(std::int32_t(0))) return bad("TimeZone.getDSTSavings stack overflow"); continue; }
                    if (target_name == "getID" && target_desc == "()Ljava/lang/String;") { JvmValue id; if (!heap_.get_field(target_receiver, "time_zone_id", id)) id = heap_.allocate_string("UTC"); if (!push(id)) return bad("TimeZone.getID stack overflow"); continue; }
                    if (target_name == "setID" && target_desc == "(Ljava/lang/String;)V") { if (!heap_.put_field(target_receiver, "time_zone_id", target_args[0])) return bad("TimeZone.setID failed"); continue; }
                }
                if (owner == "sun/misc/VM" && op == 0xb8) {
                    if (target_name == "initialize" && target_desc == "()V") continue;
                    if (target_name == "isBooted" && target_desc == "()Z") { if (!push(std::int32_t(1))) return bad("VM.isBooted stack overflow"); continue; }
                }
                if (owner == "alf" && (target_name == "b" || target_name == "c") && target_desc == "()V") {
                    // Resource-pack repository discovery uses desktop FileSystem/Security providers;
                    // keep the bundled default pack path and defer desktop pack scanning.
                    continue;
                }
                if (owner == "java/nio/charset/Charset" && op == 0xb8 &&
                    (target_name == "lookupViaProviders" || target_name == "lookupExtendedCharset") &&
                    target_desc == "(Ljava/lang/String;)Ljava/nio/charset/Charset;") {
                    if (!push(NullValue{})) return bad("Charset provider lookup stack overflow");
                    continue;
                }
                if (owner == "java/nio/charset/Charset" && op == 0xb8 &&
                    (target_name == "forName" || target_name == "isSupported") && target_desc == "(Ljava/lang/String;)" +
                    std::string(target_name == "forName" ? "Ljava/nio/charset/Charset;" : "Z")) {
                    ObjectRef name_ref=0; std::string charset_name;
                    if(!as_ref(target_args[0],name_ref)||!name_ref||!heap_.get_string(name_ref,charset_name))return bad("Charset.forName requires a name");
                    std::string normalized=charset_name;
                    std::transform(normalized.begin(),normalized.end(),normalized.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
                    const bool supported=normalized=="ISO-8859-1"||normalized=="ISO8859-1"||normalized=="UTF-8"||normalized=="UTF8"||normalized=="US-ASCII"||normalized=="ASCII"||normalized=="UTF-16"||normalized=="UTF-16BE"||normalized=="UTF-16LE";
                    if(target_name=="isSupported") { if(!push(std::int32_t(supported?1:0)))return bad("Charset.isSupported stack overflow"); continue; }
                    if(!supported) { if(raise_java_exception("java/nio/charset/UnsupportedCharsetException",charset_name,start))continue; return {false,NullValue{}}; }
                    const ObjectRef charset=heap_.allocate_object("java/nio/charset/Charset");
                    const ObjectRef name=heap_.allocate_string(charset_name);
                    if(!charset||!name||!heap_.put_field(charset,"charset_name",name)||!push(charset))return bad("Charset.forName allocation failed");
                    continue;
                }
                if (owner == "java/nio/charset/Charset" && op == 0xb8 && target_name == "defaultCharset" && target_desc == "()Ljava/nio/charset/Charset;") {
                    const ObjectRef charset=heap_.allocate_object("java/nio/charset/Charset");const ObjectRef name=heap_.allocate_string("UTF-8");
                    if (!charset || !name || !heap_.put_field(charset, "charset_name", name) || !push(charset)) return bad("Charset.defaultCharset allocation failed");
                    continue;
                }
                if (target_receiver && owner == "java/nio/charset/Charset") {
                    auto charset_name_of=[&](std::string& value){JvmValue name_value;ObjectRef name_ref=0;return heap_.get_field(target_receiver,"charset_name",name_value)&&as_ref(name_value,name_ref)&&name_ref&&heap_.get_string(name_ref,value);};
                    std::string charset_name;
                    if(!charset_name_of(charset_name))return bad("invalid Charset receiver");
                    if(target_name=="name"&&target_desc=="()Ljava/lang/String;"){ObjectRef result=heap_.allocate_string(charset_name);if(!result||!push(result))return bad("Charset.name stack overflow");continue;}
                    if(target_name=="displayName"&&(target_desc=="()Ljava/lang/String;"||target_desc=="(Ljava/util/Locale;)Ljava/lang/String;")){ObjectRef result=heap_.allocate_string(charset_name);if(!result||!push(result))return bad("Charset.displayName stack overflow");continue;}
                    if(target_name=="newEncoder"&&target_desc=="()Ljava/nio/charset/CharsetEncoder;"){ObjectRef result=heap_.allocate_object("java/nio/charset/CharsetEncoder");ObjectRef name=heap_.allocate_string(charset_name);if(!result||!name||!heap_.put_field(result,"charset_name",name)||!push(result))return bad("Charset.newEncoder allocation failed");continue;}
                    if(target_name=="newDecoder"&&target_desc=="()Ljava/nio/charset/CharsetDecoder;"){ObjectRef result=heap_.allocate_object("java/nio/charset/CharsetDecoder");ObjectRef name=heap_.allocate_string(charset_name);if(!result||!name||!heap_.put_field(result,"charset_name",name)||!push(result))return bad("Charset.newDecoder allocation failed");continue;}
                }
                if (owner == "java/nio/charset/Charset" && op == 0xb8 &&
                    target_name == "probeExtendedProvider" && target_desc == "()V") {
                    // The bundled runtime omits the optional extended-charsets provider JAR.
                    continue;
                }
                if ((owner == "ConnectedTextures" || owner == "NaturalTextures") &&
                    target_name == "update" && target_desc == "(Laaw;)V") {
                    // Optional OptiFine texture-pack post-processors are deferred until
                    // the base resource/texture pipeline and original menu are stable.
                    continue;
                }
                if (owner == "org/lwjgl/input/Controllers" && op == 0xb8) {
                    if ((target_name == "create" || target_name == "destroy" || target_name == "poll") && target_desc == "()V") continue;
                    if (target_name == "isCreated" && target_desc == "()Z") { if (!push(std::int32_t(0))) return bad("Controllers.isCreated stack overflow"); continue; }
                    if (target_name == "getControllerCount" && target_desc == "()I") { if (!push(std::int32_t(0))) return bad("Controllers.getControllerCount stack overflow"); continue; }
                }
                if (owner == "org/lwjgl/Sys" && op == 0xb8 && target_name == "initialize" && target_desc == "()V") continue;
                if (owner == "org/lwjgl/opengl/Display" && op == 0xb8) {
                    if (target_name == "createDisplayImplementation" && target_desc == "()Lorg/lwjgl/opengl/DisplayImplementation;") {
                        const ObjectRef implementation = heap_.allocate_object("org/lwjgl/opengl/DisplayImplementation");
                        if (!implementation || !push(implementation)) return bad("Switch DisplayImplementation allocation failed");
                        continue;
                    }
                    if ((target_name == "setParent" && target_desc == "(Ljava/awt/Canvas;)V") ||
                        (target_name == "setTitle" && target_desc == "(Ljava/lang/String;)V") ||
                        (target_name == "setDisplayMode" && target_desc == "(Lorg/lwjgl/opengl/DisplayMode;)V") ||
                        (target_name == "setFullscreen" && target_desc == "(Z)V") ||
                        (target_name == "setVSyncEnabled" && target_desc == "(Z)V") ||
                        (target_name == "setResizable" && target_desc == "(Z)V") ||
                        (target_name == "setLocation" && target_desc == "(II)V") ||
                        (target_name == "setSwapInterval" && target_desc == "(I)V") ||
                        (target_name == "create" && (target_desc == "()V" ||
                         target_desc.rfind("(Lorg/lwjgl/opengl/PixelFormat;", 0) == 0)) ||
                        (target_name == "update" && target_desc == "()V") ||
                        (target_name == "swapBuffers" && target_desc == "()V") ||
                        (target_name == "sync" && target_desc == "(I)V") ||
                        (target_name == "processMessages" && target_desc == "()V") ||
                        (target_name == "destroy" && target_desc == "()V")) {
                        if (target_name == "create" &&
                            !statics_.set("opti/switchport/Display", "created", std::int32_t(1)))
                            statics_.define("opti/switchport/Display", "created", std::int32_t(1));
                        if (target_name == "destroy" &&
                            !statics_.set("opti/switchport/Display", "created", std::int32_t(0)))
                            statics_.define("opti/switchport/Display", "created", std::int32_t(0));
                        if ((target_name == "update" || target_name == "swapBuffers") && gl_bridge_) gl_bridge_->display_update();
                        continue;
                    }
                    if (target_name == "isCreated" && target_desc == "()Z") {
                        JvmValue created = std::int32_t(0);
                        statics_.get("opti/switchport/Display", "created", created);
                        if (!push(created)) return bad("Display.isCreated stack overflow");
                        continue;
                    }
                    if (target_name == "isCloseRequested" && target_desc == "()Z") {
                        const bool close = input_host_ && input_host_->should_close && input_host_->should_close(input_host_->user);
                        if (!push(std::int32_t(close ? 1 : 0))) return bad("Display.isCloseRequested stack overflow");
                        continue;
                    }
                    if (target_name == "isActive" && target_desc == "()Z") {
                        if (!push(std::int32_t(1))) return bad("Display.isActive stack overflow");
                        continue;
                    }
                    if (target_name == "isVisible" && target_desc == "()Z") {
                        if (!push(std::int32_t(1))) return bad("Display.isVisible stack overflow");
                        continue;
                    }
                    if (target_name == "getDisplayMode" && target_desc == "()Lorg/lwjgl/opengl/DisplayMode;") {
                        const ObjectRef mode = heap_.allocate_object("org/lwjgl/opengl/DisplayMode");
                        if (!mode || !store_int(mode, "width", 854) || !store_int(mode, "height", 480) ||
                            !store_int(mode, "bpp", 32) || !store_int(mode, "freq", 60) ||
                            !heap_.put_field(mode, "fullscreen", std::int32_t(0)) || !push(mode))
                            return bad("Display.getDisplayMode allocation failed");
                        continue;
                    }
                    if (target_name == "getWidth" && target_desc == "()I") { if (!push(std::int32_t(854))) return bad("Display.getWidth stack overflow"); continue; }
                    if (target_name == "getHeight" && target_desc == "()I") { if (!push(std::int32_t(480))) return bad("Display.getHeight stack overflow"); continue; }
                    if (target_name == "getSwapInterval" && target_desc == "()I") { if (!push(std::int32_t(0))) return bad("Display.getSwapInterval stack overflow"); continue; }
                }
                if (target_receiver && owner == "java/io/File") {
                    auto* file_object = heap_.get(target_receiver);
                    if (!file_object) return bad("invalid File receiver");
                    auto file_arg = [&](std::size_t index, std::string& value) {
                        ObjectRef ref_value = 0;
                        if (index >= target_args.size() || !as_ref(target_args[index], ref_value) || !ref_value) return false;
                        const auto* arg_object = heap_.get(ref_value);
                        if (!arg_object) return false;
                        if (arg_object->class_name == "java/lang/String") { value = arg_object->string_value; return true; }
                        if (arg_object->class_name == "java/io/File") { value = arg_object->string_value; return true; }
                        return false;
                    };
                    if (target_name == "<init>" && target_desc == "(Ljava/lang/String;)V") {
                        std::string path;
                        if (!file_arg(0, path)) return bad("File(String) requires a String path");
                        file_object->string_value = path;
                        continue;
                    }
                    if (target_name == "<init>" && target_desc == "(Ljava/lang/String;Ljava/lang/String;)V") {
                        std::string parent, child;
                        if (!file_arg(0, parent) || !file_arg(1, child)) return bad("File(String,String) arguments invalid");
                        file_object->string_value = (std::filesystem::path(parent) / std::filesystem::path(child)).lexically_normal().string();
                        continue;
                    }
                    if (target_name == "<init>" && target_desc == "(Ljava/io/File;Ljava/lang/String;)V") {
                        std::string parent, child;
                        if (!file_arg(0, parent) || !file_arg(1, child)) return bad("File(File,String) arguments invalid");
                        file_object->string_value = (std::filesystem::path(parent) / std::filesystem::path(child)).lexically_normal().string();
                        continue;
                    }
                    const std::filesystem::path path(file_object->string_value);
                    std::error_code fs_error;
                    if (target_name == "exists" && target_desc == "()Z") {
                        const bool result = std::filesystem::exists(path, fs_error);
                        if (!push(std::int32_t(result ? 1 : 0))) return bad("File.exists stack overflow");
                        continue;
                    }
                    if (target_name == "isDirectory" && target_desc == "()Z") {
                        const bool result = std::filesystem::is_directory(path, fs_error);
                        if (!push(std::int32_t(result ? 1 : 0))) return bad("File.isDirectory stack overflow");
                        continue;
                    }
                    if (target_name == "isFile" && target_desc == "()Z") {
                        const bool result = std::filesystem::is_regular_file(path, fs_error);
                        if (!push(std::int32_t(result ? 1 : 0))) return bad("File.isFile stack overflow");
                        continue;
                    }
                    if (target_name == "mkdirs" && target_desc == "()Z") {
                        fs_error.clear();
                        const bool made = std::filesystem::create_directories(path, fs_error);
                        const bool result = !fs_error && (made || std::filesystem::is_directory(path, fs_error));
                        if (!push(std::int32_t(result ? 1 : 0))) return bad("File.mkdirs stack overflow");
                        continue;
                    }
                    if (target_name == "mkdir" && target_desc == "()Z") {
                        fs_error.clear();
                        const bool made = std::filesystem::create_directory(path, fs_error);
                        if (!push(std::int32_t(made ? 1 : 0))) return bad("File.mkdir stack overflow");
                        continue;
                    }
                    if ((target_name == "getPath" || target_name == "getAbsolutePath" || target_name == "getCanonicalPath") &&
                        target_desc == "()Ljava/lang/String;") {
                        const std::string result_text = target_name == "getPath" ? file_object->string_value :
                            std::filesystem::absolute(path, fs_error).lexically_normal().string();
                        const ObjectRef result = heap_.allocate_string(result_text);
                        if (!result || !push(result)) return bad("File path allocation failed");
                        continue;
                    }
                    if (target_name == "getName" && target_desc == "()Ljava/lang/String;") {
                        const ObjectRef result = heap_.allocate_string(path.filename().string());
                        if (!result || !push(result)) return bad("File.getName allocation failed");
                        continue;
                    }
                    if (target_name == "getParent" && target_desc == "()Ljava/lang/String;") {
                        const auto parent = path.parent_path();
                        const JvmValue result = parent.empty() ? JvmValue(NullValue{}) : JvmValue(heap_.allocate_string(parent.string()));
                        if (std::holds_alternative<NullValue>(result) || std::get<ObjectRef>(result) != 0) {
                            if (!push(result)) return bad("File.getParent stack overflow");
                            continue;
                        }
                        return bad("File.getParent allocation failed");
                    }
                    if (target_name == "canRead" && target_desc == "()Z") {
                        if (!push(std::int32_t(std::filesystem::exists(path, fs_error) ? 1 : 0))) return bad("File.canRead stack overflow");
                        continue;
                    }
                    if (target_name == "canWrite" && target_desc == "()Z") {
                        if (!push(std::int32_t(std::filesystem::exists(path, fs_error) ? 1 : 0))) return bad("File.canWrite stack overflow");
                        continue;
                    }
                    if (target_name == "length" && target_desc == "()J") {
                        const auto size = std::filesystem::is_regular_file(path, fs_error) ? std::filesystem::file_size(path, fs_error) : 0;
                        if (!push(static_cast<std::int64_t>(fs_error ? 0 : size))) return bad("File.length stack overflow");
                        continue;
                    }
                    if (target_name == "lastModified" && target_desc == "()J") {
                        if (!push(std::int64_t(0))) return bad("File.lastModified stack overflow");
                        continue;
                    }
                    if (target_name == "delete" && target_desc == "()Z") {
                        const bool removed = std::filesystem::remove(path, fs_error);
                        if (!push(std::int32_t(removed ? 1 : 0))) return bad("File.delete stack overflow");
                        continue;
                    }
                    if (target_name == "createNewFile" && target_desc == "()Z") {
                        fs_error.clear();
                        const bool existed = std::filesystem::exists(path, fs_error);
                        bool created = false;
                        if (!existed && !fs_error) {
                            std::ofstream output(path, std::ios::binary | std::ios::out);
                            created = static_cast<bool>(output);
                        }
                        if (!push(std::int32_t(created ? 1 : 0))) return bad("File.createNewFile stack overflow");
                        continue;
                    }
                    if (target_name == "deleteOnExit" && target_desc == "()V") continue;
                    if (target_name == "renameTo" && target_desc == "(Ljava/io/File;)Z") {
                        std::string destination;
                        if (!file_arg(0, destination)) return bad("File.renameTo destination invalid");
                        fs_error.clear();
                        std::filesystem::rename(path, std::filesystem::path(destination), fs_error);
                        if (!push(std::int32_t(fs_error ? 0 : 1))) return bad("File.renameTo stack overflow");
                        continue;
                    }
                    if (target_name == "isAbsolute" && target_desc == "()Z") {
                        if (!push(std::int32_t(path.is_absolute() ? 1 : 0))) return bad("File.isAbsolute stack overflow");
                        continue;
                    }
                    if (target_name == "isHidden" && target_desc == "()Z") {
                        const auto name = path.filename().string();
                        if (!push(std::int32_t(!name.empty() && name[0] == '.' ? 1 : 0))) return bad("File.isHidden stack overflow");
                        continue;
                    }
                    if (target_name == "setLastModified" && target_desc == "(J)Z") {
                        if (!push(std::int32_t(0))) return bad("File.setLastModified stack overflow");
                        continue;
                    }
                    if (target_name == "getParentFile" && target_desc == "()Ljava/io/File;") {
                        const auto parent = path.parent_path();
                        if (parent.empty()) { if (!push(NullValue{})) return bad("File.getParentFile stack overflow"); }
                        else {
                            const ObjectRef parent_ref = heap_.allocate_object("java/io/File");
                            if (!parent_ref || !heap_.get(parent_ref)) return bad("File.getParentFile allocation failed");
                            heap_.get(parent_ref)->string_value = parent.string();
                            if (!push(parent_ref)) return bad("File.getParentFile stack overflow");
                        }
                        continue;
                    }
                    if (target_name == "getAbsoluteFile" && target_desc == "()Ljava/io/File;") {
                        const ObjectRef absolute_ref = heap_.allocate_object("java/io/File");
                        if (!absolute_ref) return bad("File.getAbsoluteFile allocation failed");
                        heap_.get(absolute_ref)->string_value = std::filesystem::absolute(path, fs_error).lexically_normal().string();
                        if (!push(absolute_ref)) return bad("File.getAbsoluteFile stack overflow");
                        continue;
                    }
                    if (target_name == "list" && target_desc == "()[Ljava/lang/String;") {
                        std::vector<std::string> names;
                        fs_error.clear();
                        if (std::filesystem::is_directory(path, fs_error)) {
                            for (std::filesystem::directory_iterator it(path, fs_error), end; !fs_error && it != end; it.increment(fs_error))
                                names.push_back(it->path().filename().string());
                        }
                        const ObjectRef array = heap_.allocate_reference_array(names.size(), "java/lang/String");
                        if (!array) return bad("File.list array allocation failed");
                        for (std::size_t i = 0; i < names.size(); ++i) {
                            const ObjectRef name_ref = heap_.allocate_string(names[i]);
                            if (!name_ref || !heap_.array_store(array, i, name_ref)) return bad("File.list element allocation failed");
                        }
                        if (!push(array)) return bad("File.list stack overflow");
                        continue;
                    }
                    if (target_name == "listFiles" && (target_desc == "()[Ljava/io/File;" ||
                        target_desc == "(Ljava/io/FilenameFilter;)[Ljava/io/File;" || target_desc == "(Ljava/io/FileFilter;)[Ljava/io/File;")) {
                        std::vector<std::filesystem::path> entries;
                        fs_error.clear();
                        if (std::filesystem::is_directory(path, fs_error)) {
                            for (std::filesystem::directory_iterator it(path, fs_error), end; !fs_error && it != end; it.increment(fs_error))
                                entries.push_back(it->path());
                        }
                        const ObjectRef array = heap_.allocate_reference_array(entries.size(), "java/io/File");
                        if (!array) return bad("File.listFiles array allocation failed");
                        for (std::size_t i = 0; i < entries.size(); ++i) {
                            const ObjectRef entry_ref = heap_.allocate_object("java/io/File");
                            if (!entry_ref) return bad("File.listFiles element allocation failed");
                            heap_.get(entry_ref)->string_value = entries[i].string();
                            if (!heap_.array_store(array, i, entry_ref)) return bad("File.listFiles element copy failed");
                        }
                        if (!push(array)) return bad("File.listFiles stack overflow");
                        continue;
                    }
                    if (target_name == "toString" && target_desc == "()Ljava/lang/String;") {
                        const ObjectRef result = heap_.allocate_string(file_object->string_value);
                        if (!result || !push(result)) return bad("File.toString allocation failed");
                        continue;
                    }
                }
                if (target_receiver && owner == "aaw" && target_name == "getTextureDimensions" &&
                    target_desc == "(I)Ljava/awt/Dimension;" && gl_bridge_) {
                    const auto texture_id = static_cast<std::uint32_t>(std::get<std::int32_t>(target_args[0]));
                    const auto* texture = gl_bridge_->context().texture_store().get(texture_id);
                    if (!texture || texture->width <= 0 || texture->height <= 0) {
                        if (!push(NullValue{})) return bad("Texture dimensions lookup stack overflow");
                        continue;
                    }
                    const ObjectRef dimension = heap_.allocate_object("java/awt/Dimension");
                    if (!dimension || !store_int(dimension, "width", texture->width) ||
                        !store_int(dimension, "height", texture->height) || !push(dimension))
                        return bad("Texture dimensions allocation failed");
                    continue;
                }
                if (target_receiver && owner == "java/awt/Dimension" &&
                    target_name == "<init>" && target_desc == "(II)V") {
                    if (!store_int(target_receiver, "width", std::get<std::int32_t>(target_args[0])) ||
                        !store_int(target_receiver, "height", std::get<std::int32_t>(target_args[1])))
                        return bad("AWT Dimension initialization failed");
                    continue;
                }
                if (target_receiver && owner == "java/awt/BorderLayout" &&
                    target_name == "<init>" && target_desc == "()V") continue;

                // Minimal java.util.ArrayList needed by legacy client startup.
                if (target_receiver && (owner == "java/util/ArrayList" || owner == "java/util/List" || owner == "java/util/Collection") &&
                    (receiver_class == "java/util/ArrayList" || receiver_class == owner)) {
                    auto* list = heap_.get(target_receiver);
                    if (!list) return bad("invalid ArrayList receiver");
                    if ((target_name == "<init>" && target_desc == "()V") ||
                        (target_name == "<init>" && target_desc == "(I)V")) continue;
                    if (target_name == "add" && target_desc == "(Ljava/lang/Object;)Z") {
                        list->elements.push_back(target_args[0]);
                        if (!push(std::int32_t(1))) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "remove" && target_desc == "(Ljava/lang/Object;)Z") {
                        ObjectRef wanted_ref = 0; as_ref(target_args[0], wanted_ref);
                        std::string wanted_text; if (wanted_ref) heap_.get_string(wanted_ref, wanted_text);
                        for (auto it = list->elements.begin(); it != list->elements.end(); ++it) {
                            ObjectRef item_ref = 0; as_ref(*it, item_ref);
                            std::string item_text; const bool both_strings = item_ref && wanted_ref && heap_.get_string(item_ref, item_text);
                            const bool equal = (item_ref == wanted_ref && (!item_ref || !wanted_ref)) ||
                                (both_strings && item_text == wanted_text) ||
                                (std::holds_alternative<std::int32_t>(*it) && std::holds_alternative<std::int32_t>(target_args[0]) && std::get<std::int32_t>(*it) == std::get<std::int32_t>(target_args[0]));
                            if (equal) { list->elements.erase(it); if (!push(std::int32_t(1))) return bad("ArrayList.remove stack overflow"); goto arraylist_remove_done; }
                        }
                        if (!push(std::int32_t(0))) return bad("ArrayList.remove stack overflow");
                        arraylist_remove_done:
                        continue;
                    }
                    if (target_name == "contains" && target_desc == "(Ljava/lang/Object;)Z") {
                        ObjectRef wanted_ref = 0; as_ref(target_args[0], wanted_ref);
                        bool found = false;
                        for (const auto& item : list->elements) { ObjectRef item_ref = 0; as_ref(item, item_ref); if (item_ref == wanted_ref) { found = true; break; } }
                        if (!push(std::int32_t(found ? 1 : 0))) return bad("ArrayList.contains stack overflow");
                        continue;
                    }
                    if (target_name == "remove" && target_desc == "(I)Ljava/lang/Object;") {
                        const auto index = std::get<std::int32_t>(target_args[0]);
                        if (index < 0 || static_cast<std::size_t>(index) >= list->elements.size()) { if (raise_java_exception("java/lang/IndexOutOfBoundsException", "ArrayList index out of bounds", start)) continue; return {false, NullValue{}}; }
                        const JvmValue removed = list->elements[static_cast<std::size_t>(index)];
                        list->elements.erase(list->elements.begin() + index);
                        if (!push(removed)) return bad("ArrayList.remove stack overflow");
                        continue;
                    }
                    if (target_name == "set" && target_desc == "(ILjava/lang/Object;)Ljava/lang/Object;") {
                        const auto index = std::get<std::int32_t>(target_args[0]);
                        if (index < 0 || static_cast<std::size_t>(index) >= list->elements.size()) { if (raise_java_exception("java/lang/IndexOutOfBoundsException", "ArrayList index out of bounds", start)) continue; return {false, NullValue{}}; }
                        const JvmValue previous = list->elements[static_cast<std::size_t>(index)];
                        list->elements[static_cast<std::size_t>(index)] = target_args[1];
                        if (!push(previous)) return bad("ArrayList.set stack overflow");
                        continue;
                    }
                    if (target_name == "iterator" && target_desc == "()Ljava/util/Iterator;") {
                        const ObjectRef iterator = heap_.allocate_object("java/util/ArrayList$Itr");
                        if (!iterator || !heap_.put_field(iterator, "iterator_list", target_receiver) ||
                            !heap_.put_field(iterator, "iterator_cursor", std::int32_t(0)) || !push(iterator))
                            return bad("ArrayList.iterator allocation failed");
                        continue;
                    }
                    if (target_name == "toArray" && target_desc == "()[Ljava/lang/Object;") {
                        const ObjectRef array = heap_.allocate_reference_array(list->elements.size(),"Ljava/lang/Object;");
                        if (!array) return bad("ArrayList.toArray allocation failed");
                        for(std::size_t n=0;n<list->elements.size();++n) if(!heap_.array_store(array,n,list->elements[n])) return bad("ArrayList.toArray copy failed");
                        if(!push(array))return bad("ArrayList.toArray stack overflow");
                        continue;
                    }
                    if (target_name == "toArray" && target_desc == "([Ljava/lang/Object;)[Ljava/lang/Object;") {
                        ObjectRef array=0; as_ref(target_args[0],array); auto* destination=heap_.get(array);
                        if(!destination || destination->kind!=HeapKind::ReferenceArray) return bad("ArrayList.toArray requires reference array");
                        if(destination->elements.size()<list->elements.size()) { array=heap_.allocate_reference_array(list->elements.size(),destination->component_descriptor); destination=heap_.get(array); }
                        if(!array||!destination)return bad("ArrayList.toArray allocation failed");
                        for(std::size_t n=0;n<list->elements.size();++n) if(!heap_.array_store(array,n,list->elements[n]))return bad("ArrayList.toArray copy failed");
                        if(destination->elements.size()>list->elements.size()) heap_.array_store(array,list->elements.size(),NullValue{});
                        if(!push(array))return bad("ArrayList.toArray stack overflow");
                        continue;
                    }
                    if (target_name == "size" && target_desc == "()I") {
                        if (!push(static_cast<std::int32_t>(list->elements.size()))) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "get" && target_desc == "(I)Ljava/lang/Object;") {
                        const auto index = std::get<std::int32_t>(target_args[0]);
                        if (index < 0 || static_cast<std::size_t>(index) >= list->elements.size()) {
                            if (raise_java_exception("java/lang/IndexOutOfBoundsException", "ArrayList index out of bounds", start)) continue;
                            return {false, NullValue{}};
                        }
                        if (!push(list->elements[static_cast<std::size_t>(index)])) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "isEmpty" && target_desc == "()Z") {
                        if (!push(std::int32_t(list->elements.empty() ? 1 : 0))) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "clear" && target_desc == "()V") { list->elements.clear(); continue; }
                    if (target_name == "removeAll" && target_desc == "(Ljava/util/Collection;)Z") {
                        ObjectRef other_ref = 0; as_ref(target_args[0], other_ref);
                        const auto* other = heap_.get(other_ref);
                        if (!other) { if (!push(std::int32_t(0))) return bad("ArrayList.removeAll stack overflow"); continue; }
                        auto same_value = [&](const JvmValue& a, const JvmValue& b) {
                            ObjectRef ar=0, br=0; as_ref(a,ar); as_ref(b,br);
                            if (ar == br) return true;
                            std::string as, bs;
                            if (ar && br && heap_.get_string(ar,as) && heap_.get_string(br,bs)) return as == bs;
                            return std::holds_alternative<std::int32_t>(a) && std::holds_alternative<std::int32_t>(b) && std::get<std::int32_t>(a)==std::get<std::int32_t>(b);
                        };
                        const auto old_size = list->elements.size();
                        list->elements.erase(std::remove_if(list->elements.begin(),list->elements.end(),[&](const JvmValue& value){
                            return std::any_of(other->elements.begin(),other->elements.end(),[&](const JvmValue& candidate){return same_value(value,candidate);});
                        }),list->elements.end());
                        if (!push(std::int32_t(list->elements.size()!=old_size ? 1 : 0))) return bad("ArrayList.removeAll stack overflow");
                        continue;
                    }
                    if (target_name == "retainAll" && target_desc == "(Ljava/util/Collection;)Z") {
                        ObjectRef other_ref = 0; as_ref(target_args[0], other_ref);
                        const auto* other = heap_.get(other_ref);
                        if (!other) return bad("ArrayList.retainAll requires a collection");
                        const auto old_size = list->elements.size();
                        list->elements.erase(std::remove_if(list->elements.begin(),list->elements.end(),[&](const JvmValue& value){
                            return std::none_of(other->elements.begin(),other->elements.end(),[&](const JvmValue& candidate){
                                ObjectRef ar=0,br=0; as_ref(value,ar); as_ref(candidate,br); if(ar==br)return true;
                                std::string as,bs; return ar&&br&&heap_.get_string(ar,as)&&heap_.get_string(br,bs)&&as==bs;
                            });
                        }),list->elements.end());
                        if (!push(std::int32_t(list->elements.size()!=old_size ? 1 : 0))) return bad("ArrayList.retainAll stack overflow");
                        continue;
                    }
                }

                // IdentityHashMap is used by the original texture manager. Keep
                // reference identity (not String.equals) for object keys.
                if (target_receiver && (owner == "java/util/IdentityHashMap" ||
                    receiver_class == "java/util/IdentityHashMap" ||
                    (owner == "java/util/Map" && receiver_class == "java/util/IdentityHashMap"))) {
                    auto* map = heap_.get(target_receiver);
                    if (!map) return bad("invalid IdentityHashMap receiver");
                    if (target_name == "<init>" && (target_desc == "()V" || target_desc == "(I)V")) continue;
                    auto key_for = [&](const JvmValue& value) {
                        ObjectRef ref = 0;
                        if (as_ref(value, ref)) return std::string("identity:") + std::to_string(ref);
                        if (std::holds_alternative<std::int32_t>(value)) return std::string("primitive:") + std::to_string(std::get<std::int32_t>(value));
                        if (std::holds_alternative<std::int64_t>(value)) return std::string("long:") + std::to_string(std::get<std::int64_t>(value));
                        return std::string("null");
                    };
                    if (target_name == "put" && target_desc == "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;") {
                        const auto key = key_for(target_args[0]); JvmValue previous = NullValue{}; auto it=map->fields.find(key);
                        if(it!=map->fields.end()) previous=it->second;
                        map->fields[key]=target_args[1];
                        if(!push(previous))return bad("IdentityHashMap.put stack overflow");
                        continue;
                    }
                    if (target_name == "get" && target_desc == "(Ljava/lang/Object;)Ljava/lang/Object;") {
                        const auto it=map->fields.find(key_for(target_args[0]));
                        if(!push(it==map->fields.end()?JvmValue(NullValue{}):it->second))return bad("IdentityHashMap.get stack overflow");
                        continue;
                    }
                    if (target_name == "containsKey" && target_desc == "(Ljava/lang/Object;)Z") {
                        if(!push(std::int32_t(map->fields.count(key_for(target_args[0]))?1:0)))return bad("IdentityHashMap.containsKey stack overflow");
                        continue;
                    }
                    if (target_name == "remove" && target_desc == "(Ljava/lang/Object;)Ljava/lang/Object;") {
                        const auto it=map->fields.find(key_for(target_args[0])); JvmValue previous=NullValue{};
                        if(it!=map->fields.end()){previous=it->second;map->fields.erase(it);}
                        if(!push(previous))return bad("IdentityHashMap.remove stack overflow");
                        continue;
                    }
                    if (target_name == "size" && target_desc == "()I") { if(!push(static_cast<std::int32_t>(map->fields.size())))return bad("IdentityHashMap.size stack overflow"); continue; }
                    if (target_name == "isEmpty" && target_desc == "()Z") { if(!push(std::int32_t(map->fields.empty()?1:0)))return bad("IdentityHashMap.isEmpty stack overflow"); continue; }
                    if (target_name == "clear" && target_desc == "()V") { map->fields.clear(); continue; }
                }

                if (target_receiver && owner == "org/lwjgl/opengl/DisplayImplementation") {
                    if (target_name == "init" && target_desc == "()Lorg/lwjgl/opengl/DisplayMode;") {
                        const ObjectRef mode = heap_.allocate_object("org/lwjgl/opengl/DisplayMode");
                        if (!mode || !store_int(mode, "width", 854) || !store_int(mode, "height", 480) ||
                            !store_int(mode, "bpp", 32) || !store_int(mode, "freq", 60) ||
                            !heap_.put_field(mode, "fullscreen", std::int32_t(0)) || !push(mode))
                            return bad("Switch DisplayImplementation.init failed");
                        continue;
                    }
                    if (target_name == "getAvailableDisplayModes" && target_desc == "()[Lorg/lwjgl/opengl/DisplayMode;") {
                        const ObjectRef modes = heap_.allocate_reference_array(0, "Lorg/lwjgl/opengl/DisplayMode;");
                        if (!modes || !push(modes)) return bad("Switch display mode list allocation failed");
                        continue;
                    }
                    if ((target_name == "createWindow" && target_desc == "(Lorg/lwjgl/opengl/DrawableLWJGL;Lorg/lwjgl/opengl/DisplayMode;Ljava/awt/Canvas;II)V") ||
                        (target_name == "destroyWindow" && target_desc == "()V") ||
                        (target_name == "switchDisplayMode" && target_desc == "(Lorg/lwjgl/opengl/DisplayMode;)V") ||
                        (target_name == "resetDisplayMode" && target_desc == "()V") ||
                        (target_name == "setTitle" && target_desc == "(Ljava/lang/String;)V") ||
                        (target_name == "update" && target_desc == "()V") ||
                        (target_name == "reshape" && target_desc == "(IIII)V") ||
                        (target_name == "setResizable" && target_desc == "(Z)V") ||
                        (target_name == "setGammaRamp" && target_desc == "(Ljava/nio/FloatBuffer;)V") ||
                        (target_name == "setIcon" && target_desc == "([Ljava/nio/ByteBuffer;)I")) {
                        if (target_name == "setIcon") { if (!push(std::int32_t(0))) return bad("DisplayImplementation.setIcon stack overflow"); }
                        continue;
                    }
                    if (target_name == "getAdapter" && target_desc == "()Ljava/lang/String;") { const auto v=heap_.allocate_string("Switch host"); if(!v||!push(v))return bad("DisplayImplementation.getAdapter stack overflow"); continue; }
                    if (target_name == "getVersion" && target_desc == "()Ljava/lang/String;") { const auto v=heap_.allocate_string("OpenGL 1.1 compatibility"); if(!v||!push(v))return bad("DisplayImplementation.getVersion stack overflow"); continue; }
                    if (target_name == "isCloseRequested" && target_desc == "()Z") { const bool close=input_host_&&input_host_->should_close&&input_host_->should_close(input_host_->user); if(!push(std::int32_t(close?1:0)))return bad("DisplayImplementation close stack overflow"); continue; }
                    if (target_name == "isVisible" && target_desc == "()Z") { if(!push(std::int32_t(1)))return bad("DisplayImplementation visible stack overflow"); continue; }
                    if (target_name == "isActive" && target_desc == "()Z") { if(!push(std::int32_t(1)))return bad("DisplayImplementation active stack overflow"); continue; }
                    if (target_name == "isDirty" && target_desc == "()Z") { if(!push(std::int32_t(0)))return bad("DisplayImplementation dirty stack overflow"); continue; }
                    if (target_name == "wasResized" && target_desc == "()Z") { if(!push(std::int32_t(0)))return bad("DisplayImplementation resized stack overflow"); continue; }
                    if (target_name == "getX" && target_desc == "()I") { if(!push(std::int32_t(0)))return bad("DisplayImplementation x stack overflow"); continue; }
                    if (target_name == "getY" && target_desc == "()I") { if(!push(std::int32_t(0)))return bad("DisplayImplementation y stack overflow"); continue; }
                    if (target_name == "getPixelScaleFactor" && target_desc == "()F") { if(!push(1.0f))return bad("DisplayImplementation scale stack overflow"); continue; }
                    if (target_name == "getGammaRampLength" && target_desc == "()I") { if(!push(std::int32_t(0)))return bad("DisplayImplementation gamma stack overflow"); continue; }
                    if (target_name == "getPbufferCapabilities" && target_desc == "()I") { if(!push(std::int32_t(0)))return bad("DisplayImplementation pbuffer stack overflow"); continue; }
                }
                if (target_receiver &&
                    (owner == "org/lwjgl/opengl/InputImplementation" || owner == "org/lwjgl/opengl/DisplayImplementation")) {
                    if (target_name == "createMouse" || target_name == "destroyMouse" ||
                        target_name == "pollMouse" || target_name == "readMouse" ||
                        target_name == "grabMouse" || target_name == "setCursorPosition" ||
                        target_name == "setNativeCursor" || target_name == "createKeyboard" ||
                        target_name == "destroyKeyboard" || target_name == "pollKeyboard" ||
                        target_name == "readKeyboard" || target_name == "destroyCursor") continue;
                    if (target_name == "hasWheel" && target_desc == "()Z") { if (!push(std::int32_t(1))) return bad("InputImplementation.hasWheel stack overflow"); continue; }
                    if (target_name == "getButtonCount" && target_desc == "()I") { if (!push(std::int32_t(5))) return bad("InputImplementation.getButtonCount stack overflow"); continue; }
                    if (target_name == "getNativeCursorCapabilities" && target_desc == "()I") { if (!push(std::int32_t(0))) return bad("InputImplementation cursor capabilities stack overflow"); continue; }
                    if ((target_name == "getMinCursorSize" || target_name == "getMaxCursorSize") && target_desc == "()I") { if (!push(std::int32_t(target_name == "getMinCursorSize" ? 1 : 256))) return bad("InputImplementation cursor size stack overflow"); continue; }
                    if ((target_name == "getWidth" || target_name == "getHeight") && target_desc == "()I") { if (!push(std::int32_t(target_name == "getWidth" ? 854 : 480))) return bad("InputImplementation dimensions stack overflow"); continue; }
                    if (target_name == "isInsideWindow" && target_desc == "()Z") { if (!push(std::int32_t(1))) return bad("InputImplementation.isInsideWindow stack overflow"); continue; }
                    if (target_name == "createCursor" && target_desc == "(IIIIIILjava/nio/IntBuffer;Ljava/nio/IntBuffer;)Ljava/lang/Object;") { if (!push(NullValue{})) return bad("InputImplementation.createCursor stack overflow"); continue; }
                }
                // A console port has no desktop cursor surface. LWJGL's Cursor
                // constructor throws before Mouse.setNativeCursor is reached
                // when the backend reports zero cursor capabilities. Cursor
                // images are only desktop pointer decoration; game input is
                // provided by the controller, so retain an empty cursor object.
                if (owner == "org/lwjgl/input/Cursor" && op == 0xb7 &&
                    target_name == "<init>" &&
                    target_desc == "(IIIIILjava/nio/IntBuffer;Ljava/nio/IntBuffer;)V") {
                    continue;
                }
                // Do not install a native desktop cursor on a console backend.
                if (owner == "org/lwjgl/input/Mouse" && op == 0xb8 &&
                    target_name == "setNativeCursor" &&
                    target_desc == "(Lorg/lwjgl/input/Cursor;)Lorg/lwjgl/input/Cursor;") {
                    if (!push(NullValue{})) return bad("Mouse.setNativeCursor stack overflow");
                    continue;
                }
                // Compact HashSet model used by the original client bootstrap.
                // Values are retained as VM references/primitive keys; no host
                // pointer escapes into Java object state.
                if (target_receiver && (owner == "java/util/HashSet" ||
                    owner == "java/util/Set" || owner == "java/util/AbstractSet")) {
                    auto* set = heap_.get(target_receiver);
                    if (!set) return bad("invalid HashSet receiver");
                    auto set_key = [&](const JvmValue& value) {
                        ObjectRef object_ref = 0;
                        if (as_ref(value, object_ref)) return std::string("set:ref:") + std::to_string(object_ref);
                        if (const auto* integer = std::get_if<std::int32_t>(&value)) return std::string("set:int:") + std::to_string(*integer);
                        if (const auto* wide = std::get_if<std::int64_t>(&value)) return std::string("set:long:") + std::to_string(*wide);
                        return std::string("set:value:") + std::to_string(value.index());
                    };
                    if (target_name == "<init>" && (target_desc == "()V" || target_desc == "(I)V" || target_desc == "(IF)V")) continue;
                    if (target_name == "add" && target_desc == "(Ljava/lang/Object;)Z") {
                        const auto key = set_key(target_args[0]);
                        const bool inserted = set->fields.emplace(key, target_args[0]).second;
                        if (!push(std::int32_t(inserted ? 1 : 0))) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "contains" && target_desc == "(Ljava/lang/Object;)Z") {
                        if (!push(std::int32_t(set->fields.count(set_key(target_args[0])) ? 1 : 0))) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "remove" && target_desc == "(Ljava/lang/Object;)Z") {
                        const auto removed = set->fields.erase(set_key(target_args[0]));
                        if (!push(std::int32_t(removed ? 1 : 0))) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "iterator" && target_desc == "()Ljava/util/Iterator;") {
                        const ObjectRef iterator = heap_.allocate_object("java/util/HashSet$Itr");
                        auto* iterator_object = heap_.get(iterator);
                        if (!iterator || !iterator_object) return bad("HashSet.iterator allocation failed");
                        for (const auto& entry : set->fields)
                            if (entry.first.rfind("set:", 0) == 0) iterator_object->elements.push_back(entry.second);
                        if (!heap_.put_field(iterator, "iterator_cursor", std::int32_t(0)) || !push(iterator)) return bad("HashSet.iterator state failed");
                        continue;
                    }
                    if (target_name == "size" && target_desc == "()I") {
                        std::size_t count = 0;
                        for (const auto& entry : set->fields) if (entry.first.rfind("set:", 0) == 0) ++count;
                        if (!push(static_cast<std::int32_t>(count))) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "isEmpty" && target_desc == "()Z") {
                        const bool empty = std::none_of(set->fields.begin(), set->fields.end(),
                            [](const auto& entry) { return entry.first.rfind("set:", 0) == 0; });
                        if (!push(std::int32_t(empty ? 1 : 0))) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "clear" && target_desc == "()V") {
                        for (auto it = set->fields.begin(); it != set->fields.end();) {
                            if (it->first.rfind("set:", 0) == 0) it = set->fields.erase(it);
                            else ++it;
                        }
                        continue;
                    }
                }

                if (target_receiver && (owner == "java/util/Iterator" || receiver_class == "java/util/ArrayList$Itr" || receiver_class == "java/util/HashSet$Itr")) {
                    auto* iterator = heap_.get(target_receiver);
                    if (!iterator || (iterator->class_name != "java/util/ArrayList$Itr" && iterator->class_name != "java/util/HashSet$Itr")) return bad("unsupported Iterator implementation");
                    JvmValue list_value, cursor_value;
                    ObjectRef list_ref = 0;
                    const bool hash_set_iterator = iterator->class_name == "java/util/HashSet$Itr";
                    if ((!hash_set_iterator && (!heap_.get_field(target_receiver, "iterator_list", list_value) || !as_ref(list_value, list_ref))) ||
                        !heap_.get_field(target_receiver, "iterator_cursor", cursor_value) || !std::holds_alternative<std::int32_t>(cursor_value)) return bad("invalid collection iterator state");
                    auto* list = hash_set_iterator ? iterator : heap_.get(list_ref);
                    if (!list) return bad("iterator list was released");
                    auto cursor = std::get<std::int32_t>(cursor_value);
                    if (target_name == "hasNext" && target_desc == "()Z") {
                        if (!push(std::int32_t(cursor >= 0 && static_cast<std::size_t>(cursor) < list->elements.size() ? 1 : 0))) return bad("Iterator.hasNext stack overflow");
                        continue;
                    }
                    if (target_name == "next" && target_desc == "()Ljava/lang/Object;") {
                        if (cursor < 0 || static_cast<std::size_t>(cursor) >= list->elements.size()) { if (raise_java_exception("java/util/NoSuchElementException", "Iterator exhausted", start)) continue; return {false, NullValue{}}; }
                        const JvmValue value = list->elements[static_cast<std::size_t>(cursor)];
                        heap_.put_field(target_receiver, "iterator_cursor", std::int32_t(cursor + 1));
                        if (!push(value)) return bad("Iterator.next stack overflow");
                        continue;
                    }
                    if (target_name == "remove" && target_desc == "()V") {
                        if (cursor <= 0 || static_cast<std::size_t>(cursor) > list->elements.size()) { if (raise_java_exception("java/lang/IllegalStateException", "Iterator.remove without next", start)) continue; return {false, NullValue{}}; }
                        list->elements.erase(list->elements.begin() + (cursor - 1));
                        heap_.put_field(target_receiver, "iterator_cursor", std::int32_t(cursor - 1));
                        continue;
                    }
                }
                if (target_receiver && owner == "java/util/Random") {
                    auto* random = heap_.get(target_receiver);
                    if (!random) return bad("invalid Random receiver");
                    auto next_bits = [&](unsigned bits) -> std::uint32_t {
                        JvmValue state_value;
                        std::int64_t state = 0;
                        if (heap_.get_field(target_receiver, "random_state", state_value) && std::holds_alternative<std::int64_t>(state_value)) state = std::get<std::int64_t>(state_value);
                        state = (state * 0x5deece66dLL + 0xbLL) & ((1LL << 48) - 1);
                        heap_.put_field(target_receiver, "random_state", static_cast<std::int64_t>(state));
                        return static_cast<std::uint32_t>(static_cast<std::uint64_t>(state) >> (48 - bits));
                    };
                    if (target_name == "<init>" && target_desc == "()V") {
                        const auto seed = static_cast<std::int64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
                        heap_.put_field(target_receiver, "random_state", (seed ^ 0x5deece66dLL) & ((1LL << 48) - 1));
                        continue;
                    }
                    if (target_name == "<init>" && target_desc == "(J)V") {
                        if (!std::holds_alternative<std::int64_t>(target_args[0])) return bad("Random seed must be long");
                        const auto seed = std::get<std::int64_t>(target_args[0]);
                        heap_.put_field(target_receiver, "random_state", (seed ^ 0x5deece66dLL) & ((1LL << 48) - 1));
                        continue;
                    }
                    if (target_name == "nextInt" && target_desc == "()I") {
                        if (!push(static_cast<std::int32_t>(next_bits(32)))) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "nextInt" && target_desc == "(I)I") {
                        const auto bound = std::get<std::int32_t>(target_args[0]);
                        if (bound <= 0) { if (raise_java_exception("java/lang/IllegalArgumentException", "Random.nextInt bound must be positive", start)) continue; return {false, NullValue{}}; }
                        std::uint32_t result = 0;
                        if ((bound & -bound) == bound) result = static_cast<std::uint32_t>((static_cast<std::uint64_t>(bound) * next_bits(31)) >> 31);
                        else { std::uint32_t bits=0, val=0; do { bits=next_bits(31); val=bits%static_cast<std::uint32_t>(bound); } while (static_cast<std::int32_t>(bits-val+static_cast<std::uint32_t>(bound-1))<0); result=val; }
                        if (!push(static_cast<std::int32_t>(result))) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "nextBoolean" && target_desc == "()Z") { if (!push(static_cast<std::int32_t>(next_bits(1)))) return bad("operand stack overflow"); continue; }
                    if (target_name == "nextFloat" && target_desc == "()F") { if (!push(static_cast<float>(next_bits(24))/16777216.0f)) return bad("operand stack overflow"); continue; }
                    if (target_name == "nextDouble" && target_desc == "()D") {
                        const std::uint64_t bits=(static_cast<std::uint64_t>(next_bits(26))<<27)+next_bits(27);
                        if (!push(static_cast<double>(bits)/9007199254740992.0)) return bad("operand stack overflow");
                        continue;
                    }
                }

                if (owner == "java/util/Collections" && op == 0xb8 && target_name == "sort" &&
                    (target_desc == "(Ljava/util/List;)V" || target_desc == "(Ljava/util/List;Ljava/util/Comparator;)V")) {
                    ObjectRef list_ref = 0;
                    if (!as_ref(target_args[0], list_ref) || !list_ref) return bad("Collections.sort list is null");
                    auto* list = heap_.get(list_ref);
                    if (!list || list->class_name != "java/util/ArrayList") return bad("Collections.sort requires supported List");
                    auto sort_key = [&](const JvmValue& value) {
                        ObjectRef ref_value = 0;
                        if (as_ref(value, ref_value)) {
                            if (!ref_value) return std::string("0:");
                            const auto* object = heap_.get(ref_value);
                            if (object && object->class_name == "java/lang/String") return std::string("1:") + object->string_value;
                            if (object && object->class_name == "java/lang/Integer") {
                                JvmValue primitive; if (heap_.get_field(ref_value, "value", primitive) && std::holds_alternative<std::int32_t>(primitive)) return std::string("2:") + std::to_string(std::get<std::int32_t>(primitive) + 2147483648LL);
                            }
                            return std::string("3:") + (object ? object->class_name : std::string());
                        }
                        if (const auto* number = std::get_if<std::int32_t>(&value)) return std::string("2:") + std::to_string(static_cast<std::int64_t>(*number) + 2147483648LL);
                        return std::string("4:");
                    };
                    std::stable_sort(list->elements.begin(), list->elements.end(), [&](const JvmValue& lhs, const JvmValue& rhs) { return sort_key(lhs) < sort_key(rhs); });
                    continue;
                }
                if (target_receiver && (owner == "java/util/Properties" || owner == "java/util/Hashtable")) {
                    auto* properties = heap_.get(target_receiver);
                    if (!properties) return bad("invalid Properties receiver");
                    auto property_key = [&](const JvmValue& value, std::string& key) {
                        ObjectRef string_ref = 0;
                        return as_ref(value, string_ref) && string_ref && heap_.get_string(string_ref, key);
                    };
                    if (target_name == "<init>" && (target_desc == "()V" || target_desc == "(Ljava/util/Properties;)V")) continue;
                    if (target_name == "load" && (target_desc == "(Ljava/io/InputStream;)V" || target_desc == "(Ljava/io/Reader;)V")) {
                        ObjectRef source_ref = 0;
                        if (!as_ref(target_args[0], source_ref) || !source_ref) return bad("Properties.load source is null");
                        const auto* source = heap_.get(source_ref);
                        if (!source) return bad("Properties.load source invalid");
                        std::string contents(source->stream_bytes.begin() + static_cast<std::ptrdiff_t>(source->stream_position), source->stream_bytes.end());
                        std::size_t pos = 0;
                        while (pos < contents.size()) {
                            const auto end = contents.find_first_of("\r\n", pos);
                            std::string line = contents.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                            pos = end == std::string::npos ? contents.size() : end + 1;
                            if (end != std::string::npos && contents[end] == '\r' && pos < contents.size() && contents[pos] == '\n') ++pos;
                            const auto first = line.find_first_not_of(" \t\f");
                            if (first == std::string::npos || line[first] == '#' || line[first] == '!') continue;
                            line.erase(0, first);
                            const auto sep = line.find_first_of("=: \t\f");
                            std::string key = line.substr(0, sep), value;
                            if (sep != std::string::npos) {
                                std::size_t value_start = sep;
                                while (value_start < line.size() && (line[value_start] == ' ' || line[value_start] == '\t' || line[value_start] == '\f')) ++value_start;
                                if (value_start < line.size() && (line[value_start] == '=' || line[value_start] == ':')) ++value_start;
                                while (value_start < line.size() && (line[value_start] == ' ' || line[value_start] == '\t' || line[value_start] == '\f')) ++value_start;
                                value = line.substr(value_start);
                            }
                            if (!key.empty()) {
                                const ObjectRef value_ref = heap_.allocate_string(value);
                                if (!value_ref) return bad("Properties value allocation failed");
                                properties->fields["property:" + key] = value_ref;
                            }
                        }
                        continue;
                    }
                    if (target_name == "getProperty" && (target_desc == "(Ljava/lang/String;)Ljava/lang/String;" || target_desc == "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;")) {
                        std::string key;
                        if (!property_key(target_args[0], key)) return bad("Properties key is not String");
                        const auto it = properties->fields.find("property:" + key);
                        JvmValue result = it == properties->fields.end() ? JvmValue(NullValue{}) : it->second;
                        if (it == properties->fields.end() && target_args.size() == 2) result = target_args[1];
                        if (!push(result)) return bad("Properties.getProperty stack overflow");
                        continue;
                    }
                    if (target_name == "setProperty" && target_desc == "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/Object;") {
                        std::string key;
                        if (!property_key(target_args[0], key)) return bad("Properties key is not String");
                        const auto previous = properties->fields.find("property:" + key);
                        JvmValue old = previous == properties->fields.end() ? JvmValue(NullValue{}) : previous->second;
                        properties->fields["property:" + key] = target_args[1];
                        if (!push(old)) return bad("Properties.setProperty stack overflow");
                        continue;
                    }
                    if (target_name == "containsKey" && target_desc == "(Ljava/lang/Object;)Z") {
                        std::string key;
                        const bool valid = property_key(target_args[0], key);
                        if (!push(std::int32_t(valid && properties->fields.count("property:" + key) ? 1 : 0))) return bad("Properties.containsKey stack overflow");
                        continue;
                    }
                }

                if (target_receiver &&
                    (receiver_class == "java/util/HashMap" || receiver_class == "java/util/TreeMap" ||
                     receiver_class == "java/util/Hashtable" || receiver_class == "java/util/Properties" ||
                     owner == "java/util/HashMap" || owner == "java/util/TreeMap" ||
                     owner == "java/util/Hashtable" || owner == "java/util/Properties" || owner == "java/util/Map" ||
                     owner == "java/util/AbstractMap" || owner == "java/util/Dictionary")) {
                    auto* map = heap_.get(target_receiver);
                    if (!map) return bad("invalid HashMap receiver");
                    if ((target_name == "<init>" && target_desc == "()V") ||
                        (target_name == "<init>" && target_desc == "(I)V") ||
                        (target_name == "<init>" && target_desc == "(IF)V")) continue;
                    auto map_key = [&](const JvmValue& key) {
                        ObjectRef key_ref = 0; std::string text;
                        if (as_ref(key, key_ref) && key_ref && heap_.get_string(key_ref, text)) return std::string("map:") + text;
                        if (std::holds_alternative<std::int32_t>(key)) return std::string("map:#") + std::to_string(std::get<std::int32_t>(key));
                        return std::string("map:@") + std::to_string(key_ref);
                    };
                    if (target_name == "put" && target_desc == "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;") {
                        const auto key = map_key(target_args[0]);
                        const auto previous = map->fields.find(key);
                        const JvmValue old = previous == map->fields.end() ? JvmValue(NullValue{}) : previous->second;
                        map->fields[key] = target_args[1];
                        if (!push(old)) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "get" && target_desc == "(Ljava/lang/Object;)Ljava/lang/Object;") {
                        const auto it = map->fields.find(map_key(target_args[0]));
                        if (!push(it == map->fields.end() ? JvmValue(NullValue{}) : it->second)) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "containsKey" && target_desc == "(Ljava/lang/Object;)Z") {
                        if (!push(std::int32_t(map->fields.count(map_key(target_args[0])) ? 1 : 0))) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "values" && target_desc == "()Ljava/util/Collection;") {
                        const ObjectRef values = heap_.allocate_object("java/util/ArrayList");
                        auto* list = heap_.get(values);
                        if (!values || !list) return bad("Map.values allocation failed");
                        for (const auto& entry : map->fields)
                            if (entry.first.rfind("map:", 0) == 0) list->elements.push_back(entry.second);
                        if (!push(values)) return bad("Map.values stack overflow");
                        continue;
                    }
                    if (target_name == "keySet" && target_desc == "()Ljava/util/Set;") {
                        const ObjectRef keys = heap_.allocate_object("java/util/HashSet");
                        auto* set = heap_.get(keys);
                        if (!keys || !set) return bad("Map.keySet allocation failed");
                        for (const auto& entry : map->fields) {
                            if (entry.first.rfind("map:", 0) != 0) continue;
                            const ObjectRef key_ref = heap_.allocate_string(entry.first.substr(4));
                            if (!key_ref) return bad("Map.keySet key allocation failed");
                            set->fields[std::string("ref:") + std::to_string(key_ref)] = key_ref;
                        }
                        if (!push(keys)) return bad("Map.keySet stack overflow");
                        continue;
                    }
                    if (target_name == "size" && target_desc == "()I") {
                        std::size_t count = 0;
                        for (const auto& entry : map->fields) if (entry.first.rfind("map:", 0) == 0) ++count;
                        if (!push(static_cast<std::int32_t>(count))) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "isEmpty" && target_desc == "()Z") {
                        const bool empty = std::none_of(map->fields.begin(), map->fields.end(),
                            [](const auto& entry) { return entry.first.rfind("map:", 0) == 0; });
                        if (!push(std::int32_t(empty ? 1 : 0))) return bad("operand stack overflow");
                        continue;
                    }
                    if (target_name == "clear" && target_desc == "()V") {
                        for (auto it = map->fields.begin(); it != map->fields.end();) {
                            if (it->first.rfind("map:", 0) == 0) it = map->fields.erase(it);
                            else ++it;
                        }
                        continue;
                    }
                }

                // Fast host path for OptiFine's RenderEngine texture upload when
                // mipmaps, anisotropic filtering and antialiasing are disabled.
                // The original Java method performs a long interpreted loop over
                // every pixel; doing the same conversion in native code preserves
                // its RGBA upload semantics without spending tens of millions of
                // VM dispatches on a single 256x256 atlas.
                if (owner == "aaw" && target_name == "a" &&
                    target_desc == "(Ljava/awt/image/BufferedImage;I)V" &&
                    target_receiver && receiver_class == "aaw" && gl_bridge_) {
                    JvmValue image_value, single_value, clamp_value, blur_value, pack_value;
                    ObjectRef image_ref = 0;
                    const bool have_image = as_ref(target_args[0], image_ref) && image_ref &&
                        heap_.get_field(image_ref, "image_pixels", image_value);
                    const bool single_tile = heap_.get_field(target_receiver, "singleTileTexture", single_value) &&
                        std::holds_alternative<std::int32_t>(single_value) && std::get<std::int32_t>(single_value) != 0;
                    ObjectRef settings_ref = 0;
                    JvmValue settings_value;
                    if (statics_.get("Config", "gameSettings", settings_value)) as_ref(settings_value, settings_ref);
                    auto setting_int = [&](const char* key, int fallback) {
                        JvmValue value;
                        return settings_ref && heap_.get_field(settings_ref, key, value) &&
                            std::holds_alternative<std::int32_t>(value) ? std::get<std::int32_t>(value) : fallback;
                    };
                    const bool simple_texture_path = setting_int("ofMipmapLevel", 0) == 0 &&
                        setting_int("ofAfLevel", 1) <= 1 && setting_int("ofAaLevel", 0) <= 0 && !single_tile;
                    if (have_image && simple_texture_path) {
                        ObjectRef pixels_ref = 0;
                        const auto* image_pixels_value = heap_.get(image_ref);
                        if (!image_pixels_value || !as_ref(image_value, pixels_ref)) return bad("RenderEngine image pixel array missing");
                        const auto* pixels = heap_.get(pixels_ref);
                        JvmValue width_value, height_value;
                        if (!pixels || pixels->kind != HeapKind::IntArray ||
                            !heap_.get_field(image_ref, "image_width", width_value) ||
                            !heap_.get_field(image_ref, "image_height", height_value) ||
                            !std::holds_alternative<std::int32_t>(width_value) ||
                            !std::holds_alternative<std::int32_t>(height_value))
                            return bad("RenderEngine image dimensions or pixels invalid");
                        const int width = std::get<std::int32_t>(width_value);
                        const int height = std::get<std::int32_t>(height_value);
                        const int texture_id = std::get<std::int32_t>(target_args[1]);
                        const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
                        if (width <= 0 || height <= 0 || count != pixels->elements.size() || count > 16000000u)
                            return bad("RenderEngine image dimensions out of range");
                        JvmValue terrain_value, gui_value, ctm_value;
                        heap_.get_field(target_receiver, "terrainTextureId", terrain_value);
                        heap_.get_field(target_receiver, "guiItemsTextureId", gui_value);
                        heap_.get_field(target_receiver, "ctmTextureId", ctm_value);
                        const auto field_int = [](const JvmValue& v) { return std::holds_alternative<std::int32_t>(v) ? std::get<std::int32_t>(v) : -1; };
                        const std::string current_texture_path = texture_resource_paths_.empty() ? std::string{} : texture_resource_paths_.back();
                        const bool atlas = texture_id == field_int(terrain_value) || texture_id == field_int(gui_value) || texture_id == field_int(ctm_value) ||
                            current_texture_path == "/terrain.png" || current_texture_path == "/gui/items.png" || current_texture_path == "/ctm.png";
                        bool anaglyph = false;
                        if (heap_.get_field(target_receiver, "k", pack_value)) {
                            ObjectRef pack_ref = 0;
                            JvmValue anaglyph_value;
                            if (as_ref(pack_value, pack_ref) && pack_ref && heap_.get_field(pack_ref, "g", anaglyph_value) &&
                                std::holds_alternative<std::int32_t>(anaglyph_value)) anaglyph = std::get<std::int32_t>(anaglyph_value) != 0;
                        }
                        std::vector<std::uint8_t> rgba(count * 4u);
                        for (std::size_t i = 0; i < count; ++i) {
                            if (!std::holds_alternative<std::int32_t>(pixels->elements[i])) return bad("RenderEngine pixel array contains non-integer");
                            const std::uint32_t argb = static_cast<std::uint32_t>(std::get<std::int32_t>(pixels->elements[i]));
                            const std::uint8_t alpha = static_cast<std::uint8_t>(argb >> 24);
                            std::uint8_t red = static_cast<std::uint8_t>(argb >> 16);
                            std::uint8_t green = static_cast<std::uint8_t>(argb >> 8);
                            std::uint8_t blue = static_cast<std::uint8_t>(argb);
                            if (alpha == 0) {
                                red = green = blue = atlas ? 255 : 0;
                            } else if (anaglyph) {
                                const int r = red, g = green, b = blue;
                                red = static_cast<std::uint8_t>((r * 30 + g * 59 + b * 11) / 100);
                                green = static_cast<std::uint8_t>((r * 30 + g * 70) / 100);
                                blue = static_cast<std::uint8_t>((r * 30 + b * 70) / 100);
                            }
                            const std::size_t offset = i * 4u;
                            rgba[offset] = red; rgba[offset + 1] = green;
                            rgba[offset + 2] = blue; rgba[offset + 3] = alpha;
                        }
                        const bool clamp = heap_.get_field(target_receiver, "b", clamp_value) &&
                            std::holds_alternative<std::int32_t>(clamp_value) && std::get<std::int32_t>(clamp_value) != 0;
                        const bool blur = heap_.get_field(target_receiver, "c", blur_value) &&
                            std::holds_alternative<std::int32_t>(blur_value) && std::get<std::int32_t>(blur_value) != 0;
                        gl_bridge_->bind_texture(0x0DE1u, static_cast<std::uint32_t>(texture_id));
                        gl_bridge_->tex_parameter_i(0x0DE1u, 0x2801u, 9728);
                        gl_bridge_->tex_parameter_i(0x0DE1u, 0x2800u, blur ? 9729 : 9728);
                        gl_bridge_->tex_parameter_i(0x0DE1u, 0x2802u, clamp ? 10496 : 10497);
                        gl_bridge_->tex_parameter_i(0x0DE1u, 0x2803u, clamp ? 10496 : 10497);
                        auto dimension = heap_.allocate_object("java/awt/Dimension");
                        if (!dimension || !heap_.put_field(dimension, "width", std::int32_t(width)) ||
                            !heap_.put_field(dimension, "height", std::int32_t(height))) return bad("RenderEngine texture dimension allocation failed");
                        auto dimension_method = resolve_method("aaw", "setTextureDimension", "(ILjava/awt/Dimension;)V", InvokeKind::Special, "aaw");
                        if (dimension_method.method && dimension_method.owner) {
                            const auto dimension_result = execute(*dimension_method.owner, *dimension_method.method,
                                {std::int32_t(texture_id), dimension}, target_receiver, depth + 1);
                            if (!dimension_result.first) return {false, NullValue{}};
                        }
                        gl_bridge_->tex_image_2d(0x0DE1u, 0, 0x1908, width, height, 0, 0x1908, 0x1401u,
                            rgba.data(), rgba.size());
                        continue;
                    }
                }

                execution_frames_.update(pc, locals, stack, pending_exception);
                auto resolved = resolve_method(owner, target_name, target_desc, kind, receiver_class);
                if (!resolved.method || !resolved.owner) {
                    // Legacy fixture compatibility: a synthetic constructor can be a no-op
                    // only when no class runtime was supplied. Cross-class calls must resolve.
                    if (op == 0xb7 && target_name == "<init>" && classes_ == nullptr) continue;
                    return bad("method resolution failed: " + owner + "." + target_name + target_desc);
                }
                const bool callee_static = (resolved.method->access_flags & 0x0008) != 0;
                if ((op == 0xb8) != callee_static) return bad("invoke opcode does not match method staticness");
                bool texture_path_pushed=false;
                if(owner=="aaw"&&target_name=="b"&&target_desc=="(Ljava/lang/String;)I"&&!target_args.empty()) {
                    ObjectRef path_ref=0; std::string path;
                    if(as_ref(target_args[0],path_ref)&&heap_.get_string(path_ref,path)) {
                        texture_resource_paths_.push_back(std::move(path)); texture_path_pushed=true;
                    }
                }
                auto called = execute(*resolved.owner, *resolved.method, target_args, target_receiver, depth + 1);
                if(texture_path_pushed) texture_resource_paths_.pop_back();
                if (!called.first) {
                    // A Java exception escaping the callee is offered to the caller's
                    // handlers at the invoke instruction PC. If no handler matches,
                    // leave it pending so the next outer frame can attempt to catch it.
                    if (pending_exception != 0) {
                        const auto* thrown = heap_.get(pending_exception);
                        if (thrown && thrown->kind == HeapKind::Object) {
                            ExceptionHandlerMatch match;
                            if (classes_) {
                                match = find_exception_handler(cls, method,
                                    static_cast<std::uint16_t>(start), thrown->class_name, *classes_);
                            } else {
                                for (const auto& handler : method.exception_table) {
                                    if (start >= handler.start_pc && start < handler.end_pc &&
                                        handler.catch_type == 0 && handler.handler_pc < method.code.size()) {
                                        match = {true, handler.handler_pc, 0};
                                        break;
                                    }
                                }
                            }
                            if (match.found && match.handler_pc < method.code.size()) {
                                stack.clear();
                                if (!push(pending_exception)) return bad("exception handler stack overflow");
                                pc = match.handler_pc;
                                pending_exception = 0;
                                execution_error.clear();
                                continue;
                            }
                        }
                    }
                    return {false, NullValue{}};
                }
                if (target_return != "V" && !push(called.second)) return bad("operand stack overflow after method return");
                continue;
            }
            if (op == 0xbc) {
                if (!need(1)) return bad("truncated newarray type");
                const auto array_type = method.code[pc++];
                if (array_type < 4 || array_type > 11) return bad("invalid newarray primitive type");
                if (!pop(x) || !as_int(x, a)) return bad("newarray length stack/type error");
                if (a < 0) {
                    if (raise_java_exception("java/lang/NegativeArraySizeException", "NegativeArraySizeException: negative array length", start)) continue;
                    return {false, NullValue{}};
                }
                static const char primitive_descriptors[] = {'\0','\0','\0','\0','Z','C','F','D','B','S','I','J'};
                const auto id = heap_.allocate_primitive_array(static_cast<std::size_t>(a), primitive_descriptors[array_type]);
                if (!id || !push(id)) return bad("primitive array allocation failed");
                continue;
            }
            if (op == 0xc5) { // multianewarray
                if (!need(3) || !classes_) return bad("multianewarray requires class runtime");
                const auto class_index = u2(method.code, pc); pc += 2;
                const auto dimensions = method.code[pc++];
                if (!class_index || class_index >= cls.constant_pool.size() || cls.constant_pool[class_index].tag != 7 ||
                    cls.constant_pool[class_index].a >= cls.constant_pool.size() || dimensions == 0)
                    return bad("invalid multianewarray operands");
                const std::string descriptor = cls.constant_pool[cls.constant_pool[class_index].a].utf8;
                if (descriptor.empty() || descriptor.front() != '[') return bad("multianewarray constant is not an array descriptor");
                std::vector<std::size_t> lengths(dimensions);
                for (std::size_t i = dimensions; i > 0; --i) {
                    if (!pop(x) || !as_int(x, a)) return bad("multianewarray dimension stack/type error");
                    if (a < 0) {
                        if (raise_java_exception("java/lang/NegativeArraySizeException", "NegativeArraySizeException: negative dimension", start)) break;
                        return {false, NullValue{}};
                    }
                    lengths[i - 1] = static_cast<std::size_t>(a);
                }
                if (pending_exception != 0) continue;
                std::function<ObjectRef(const std::string&, std::size_t)> allocate_array =
                    [&](const std::string& array_descriptor, std::size_t level) -> ObjectRef {
                        if (array_descriptor.size() < 2 || array_descriptor.front() != '[' || level >= lengths.size()) return 0;
                        const char component = array_descriptor[1];
                        if (component == '[') {
                            const std::string child_descriptor = array_descriptor.substr(1);
                            const ObjectRef array = heap_.allocate_reference_array(lengths[level], child_descriptor);
                            if (!array) return 0;
                            if (level + 1 < lengths.size()) {
                                for (std::size_t i = 0; i < lengths[level]; ++i) {
                                    const ObjectRef child = allocate_array(child_descriptor, level + 1);
                                    if (!child || !heap_.array_store(array, i, child)) return 0;
                                }
                            }
                            return array;
                        }
                        if (std::string("ZBCSIJFD").find(component) == std::string::npos) return 0;
                        return heap_.allocate_primitive_array(lengths[level], component);
                    };
                const ObjectRef array = allocate_array(descriptor, 0);
                if (!array || !push(array)) return bad("multianewarray allocation failed");
                continue;
            }
            if (op == 0xbd) {
                if (!need(2) || !classes_) return bad("anewarray requires class runtime");
                const auto ci=u2(method.code,pc);pc+=2;
                if (!ci||ci>=cls.constant_pool.size()||cls.constant_pool[ci].tag!=7||cls.constant_pool[ci].a>=cls.constant_pool.size()) return bad("invalid anewarray class constant");
                if (!pop(x)||!as_int(x,a)) return bad("anewarray length stack/type error");
                if (a<0) { if (raise_java_exception("java/lang/NegativeArraySizeException","NegativeArraySizeException: negative array length",start)) continue; return {false,NullValue{}}; }
                const auto component=cls.constant_pool[cls.constant_pool[ci].a].utf8;
                const auto id=heap_.allocate_reference_array(static_cast<std::size_t>(a),component);
                if (!id||!push(id)) return bad("reference array allocation failed");
                continue;
            }
            if (op == 0xbe) {
                if (!pop(x) || !as_ref(x, ref)) return bad("arraylength stack/type error");
                if (!ref) {
                    if (raise_java_exception("java/lang/NullPointerException", "NullPointerException: arraylength", start)) continue;
                    return {false, NullValue{}};
                }
                const auto* object = heap_.get(ref);
                if (!object || object->kind == HeapKind::Object) return bad("arraylength on non-array");
                if (!push(static_cast<std::int32_t>(object->elements.size()))) return bad("operand stack overflow");
                continue;
            }
            if (op == 0x2e || op == 0x2f || op == 0x30 || op == 0x31 || op == 0x32 || op == 0x33 || op == 0x34 || op == 0x35 ||
                op == 0x4f || op == 0x50 || op == 0x51 || op == 0x52 || op == 0x53 || op == 0x54 || op == 0x55 || op == 0x56) {
                JvmValue value, index, array;
                const bool is_store = op >= 0x4f;
                const bool is_ref_array = op == 0x32 || op == 0x53;
                if (!is_store) {
                    if (!pop(index) || !pop(array) || !as_int(index, b) || !as_ref(array, ref))
                        return bad("iaload stack/type error");
                } else {
                    if (!pop(value) || !pop(index) || !pop(array) || !as_int(index, a) || !as_ref(array, ref))
                        return bad("array store stack/type error");
                    b = a;
                }
                if (!ref) {
                    if (raise_java_exception("java/lang/NullPointerException", is_store ? "NullPointerException: array store" : "NullPointerException: array load", start)) continue;
                    return {false, NullValue{}};
                }
                const auto* array_object = heap_.get(ref);
                if (!array_object || array_object->kind == HeapKind::Object)
                    return bad("array opcode on non-array object");
                const char component = array_object->kind == HeapKind::ReferenceArray ? 'L' :
                    (array_object->component_descriptor.empty() ? 'I' : array_object->component_descriptor[0]);
                const char expected = (op == 0x2e || op == 0x4f) ? 'I' :
                    (op == 0x2f || op == 0x50) ? 'J' :
                    (op == 0x30 || op == 0x51) ? 'F' :
                    (op == 0x31 || op == 0x52) ? 'D' :
                    (op == 0x32 || op == 0x53) ? 'L' :
                    (op == 0x33 || op == 0x54) ? 'B' :
                    (op == 0x34 || op == 0x55) ? 'C' : 'S';
                const bool int_family = (expected == 'B' && (component == 'B' || component == 'Z')) ||
                    (expected == 'I' && component == 'I');
                if (component != expected && !int_family)
                    return bad("array opcode/component type mismatch");
                if (b < 0 || static_cast<std::size_t>(b) >= array_object->elements.size()) {
                    if (raise_java_exception("java/lang/ArrayIndexOutOfBoundsException", "ArrayIndexOutOfBoundsException: index out of bounds", start)) continue;
                    return {false, NullValue{}};
                }
                if (!is_store) {
                    if (!heap_.array_load(ref, static_cast<std::size_t>(b), x)) return bad("array load failed for valid index");
                    if (is_ref_array && !std::holds_alternative<ObjectRef>(x) && !std::holds_alternative<NullValue>(x)) return bad("aaload on non-reference array");
                    if (!push(x)) return bad("operand stack overflow");
                } else if (!heap_.array_store(ref, static_cast<std::size_t>(b), value)) {
                    return bad("array store failed for valid index");
                }
                continue;
            }
            if (op == 0xc2 || op == 0xc3) { // monitorenter / monitorexit
                if (!pop(x) || !as_ref(x, ref)) return bad("monitor opcode requires an object reference");
                if (!ref) {
                    if (raise_java_exception("java/lang/NullPointerException", "NullPointerException: monitor operation", start)) continue;
                    return {false, NullValue{}};
                }
                // The host probe executes the original client on one interpreter thread.
                // Preserve JVM stack/null semantics here; contended monitor scheduling
                // is implemented by the Switch thread scheduler, not this single-thread probe.
                continue;
            }
            if (op == 0xc0 || op == 0xc1) {
                if (!need(2) || !classes_) return bad("checkcast/instanceof requires registered class runtime");
                const auto ci = u2(method.code, pc); pc += 2;
                if (!ci || ci >= cls.constant_pool.size() || cls.constant_pool[ci].tag != 7 || cls.constant_pool[ci].a >= cls.constant_pool.size()) return bad("invalid type constant");
                const auto expected = cls.constant_pool[cls.constant_pool[ci].a].utf8;
                if (!pop(x) || !as_ref(x, ref)) return bad("invalid checkcast/instanceof operand");
                const bool matches = ref == 0 || classes_->instance_of(heap_, ref, expected);
                if (op == 0xc0) {
                    if (!matches) return bad("ClassCastException: object is not assignable to " + expected);
                    if (!push(x)) return bad("operand stack overflow");
                } else {
                    if (!push(std::int32_t(ref != 0 && matches))) return bad("operand stack overflow");
                }
                continue;
            }
            if (op == 0xac || op == 0xad || op == 0xae || op == 0xaf || op == 0xb0) {
                if (!pop(output.value)) return bad("return stack underflow");
                return {true, output.value};
            }
            if (op == 0xb1) return {true, NullValue{}};
            return bad("unsupported opcode " + std::to_string(op) + " at bytecode offset " + std::to_string(start) + " in " + cls.this_class + "." + method.name);
        }
        return bad("method ended without a return instruction");
    };

    const auto result = execute(entry_class, *entry, args, 0, 0);
    output.success = result.first;
    output.value = result.second;
    output.error = execution_error;
    output.instructions_executed = executed;
    return output;
}
}
