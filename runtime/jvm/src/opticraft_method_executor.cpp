#include "opticraft_method_executor.h"
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint32_t kMaxCallDepth = 64;
constexpr uint16_t kAccStatic = 0x0008;
constexpr uint16_t kAccNative = 0x0100;
constexpr uint16_t kAccAbstract = 0x0400;

struct MethodView {
    const OptiClassFile *file = nullptr;
    const OptiLoadedClass *loaded = nullptr;
};

struct ExecutionState {
    std::unordered_map<std::string, const OptiLoadedClass *> loaded_classes;
    std::vector<OptiLoadedClass *> owned_classes;

    ~ExecutionState() {
        for (OptiLoadedClass *loaded : owned_classes) {
            opticraft_loaded_class_destroy(loaded);
        }
    }
};

struct InvokeContext {
    MethodView view;
    ExecutionState *state = nullptr;
    uint32_t depth = 0;
};

OptiExecResult failed(OptiExecError error) {
    OptiExecResult result{};
    result.status = OPTI_EXEC_ERROR;
    result.error = error;
    return result;
}

bool class_name(const MethodView &view, const char **name, uint16_t *length) {
    return view.loaded != nullptr
        ? opticraft_loaded_class_name(view.loaded, 0, name, length) != 0
        : opticraft_classfile_class_name(view.file, 0, name, length) != 0;
}

bool find_method(const MethodView &view, const char *name,
                 const char *descriptor, uint16_t *access_flags) {
    return view.loaded != nullptr
        ? opticraft_loaded_class_find_method(view.loaded, name, descriptor,
                                              access_flags, nullptr) != 0
        : opticraft_classfile_find_method(view.file, name, descriptor,
                                           access_flags, nullptr) != 0;
}

bool method_code(const MethodView &view, const char *name,
                 const char *descriptor, const uint8_t **code,
                 uint32_t *code_length, uint16_t *max_stack,
                 uint16_t *max_locals) {
    return view.loaded != nullptr
        ? opticraft_loaded_class_method_code(view.loaded, name, descriptor,
                                              code, code_length, max_stack,
                                              max_locals) != 0
        : opticraft_classfile_method_code(view.file, name, descriptor, code,
                                           code_length, max_stack, max_locals) != 0;
}

bool method_reference(const MethodView &view, uint16_t cp_index,
                      OptiMethodReference *reference) {
    return view.loaded != nullptr
        ? opticraft_loaded_class_method_ref(view.loaded, cp_index,
                                             reference) != 0
        : opticraft_classfile_method_ref(view.file, cp_index, reference) != 0;
}

bool parse_i32_descriptor(const char *descriptor, size_t *argument_count,
                           bool *returns_i32) {
    if (descriptor == nullptr || argument_count == nullptr ||
        returns_i32 == nullptr || *descriptor++ != '(') {
        return false;
    }

    size_t count = 0;
    while (*descriptor != '\0' && *descriptor != ')') {
        if (*descriptor++ != 'I') return false;
        ++count;
    }
    if (*descriptor++ != ')') return false;

    bool returns_value = false;
    if (descriptor[0] == 'I' && descriptor[1] == '\0') {
        returns_value = true;
    } else if (descriptor[0] != 'V' || descriptor[1] != '\0') {
        return false;
    }
    *argument_count = count;
    *returns_i32 = returns_value;
    return true;
}

OptiExecResult execute_method(const MethodView &view, const char *name,
                              const char *descriptor, int32_t *locals,
                              size_t local_count, int32_t *stack,
                              size_t stack_capacity, uint32_t max_steps,
                              uint32_t depth, ExecutionState *state);

OptiInvokeResult invoke_static(void *opaque, uint16_t cp_index,
                               const int32_t *operand_stack,
                               size_t operand_stack_size,
                               uint32_t step_budget) {
    auto *context = static_cast<InvokeContext *>(opaque);
    OptiInvokeResult result{};
    result.status = OPTI_EXEC_ERROR;
    result.error = OPTI_EXEC_ERR_METHOD_REF;

    if (context == nullptr || context->depth >= kMaxCallDepth) {
        result.error = context == nullptr ? OPTI_EXEC_ERR_METHOD_REF
                                          : OPTI_EXEC_ERR_CALL_DEPTH;
        return result;
    }

    OptiMethodReference reference{};
    if (!method_reference(context->view, cp_index, &reference)) return result;

    const char *current_class = nullptr;
    uint16_t current_class_length = 0;
    if (!class_name(context->view, &current_class, &current_class_length)) {
        return result;
    }

    MethodView target_view = context->view;
    if (current_class_length != reference.class_name_length ||
        std::memcmp(current_class, reference.class_name,
                    current_class_length) != 0) {
        if (context->state == nullptr || context->view.loaded == nullptr) {
            return result;
        }
        try {
            const std::string owner(reference.class_name,
                                    reference.class_name_length);
            auto found = context->state->loaded_classes.find(owner);
            if (found == context->state->loaded_classes.end()) {
                OptiLoadedClass *dependency = nullptr;
                if (!opticraft_loaded_class_load_sibling(
                        context->view.loaded, owner.c_str(), &dependency)) {
                    return result;
                }
                try {
                    context->state->owned_classes.push_back(dependency);
                } catch (...) {
                    opticraft_loaded_class_destroy(dependency);
                    result.error = OPTI_EXEC_ERR_OUT_OF_MEMORY;
                    return result;
                }
                try {
                    found = context->state->loaded_classes.emplace(
                        owner, dependency).first;
                } catch (...) {
                    result.error = OPTI_EXEC_ERR_OUT_OF_MEMORY;
                    return result;
                }
            }
            target_view = MethodView{nullptr, found->second};
        } catch (...) {
            result.error = OPTI_EXEC_ERR_OUT_OF_MEMORY;
            return result;
        }
    }

    const char *method_name = reference.name;
    const char *method_descriptor = reference.descriptor;
    size_t argument_count = 0;
    bool returns_i32 = false;
    if (!parse_i32_descriptor(method_descriptor, &argument_count,
                              &returns_i32)) {
        result.error = OPTI_EXEC_ERR_METHOD_DESCRIPTOR;
        return result;
    }
    result.argument_count = argument_count;
    if (argument_count > operand_stack_size) {
        result.error = OPTI_EXEC_ERR_STACK_UNDERFLOW;
        return result;
    }

    uint16_t access_flags = 0;
    if (!find_method(target_view, method_name, method_descriptor,
                     &access_flags) ||
        (access_flags & kAccStatic) == 0 ||
        (access_flags & (kAccNative | kAccAbstract)) != 0) {
        result.error = OPTI_EXEC_ERR_METHOD_ACCESS;
        return result;
    }

    const uint8_t *code = nullptr;
    uint32_t code_length = 0;
    uint16_t max_stack = 0;
    uint16_t max_locals = 0;
    if (!method_code(target_view, method_name, method_descriptor, &code,
                     &code_length, &max_stack, &max_locals) ||
        argument_count > max_locals) {
        result.error = argument_count > max_locals
            ? OPTI_EXEC_ERR_LOCAL_BOUNDS : OPTI_EXEC_ERR_METHOD_REF;
        return result;
    }

    std::vector<int32_t> callee_locals;
    std::vector<int32_t> callee_stack;
    try {
        callee_locals.assign(max_locals, 0);
        callee_stack.assign(max_stack, 0);
    } catch (...) {
        result.error = OPTI_EXEC_ERR_OUT_OF_MEMORY;
        return result;
    }
    if (argument_count != 0) {
        const size_t first_argument = operand_stack_size - argument_count;
        for (size_t i = 0; i < argument_count; ++i) {
            callee_locals[i] = operand_stack[first_argument + i];
        }
    }

    const OptiExecResult execution = execute_method(
        target_view, method_name, method_descriptor,
        callee_locals.data(), callee_locals.size(), callee_stack.data(),
        callee_stack.size(), step_budget, context->depth + 1, context->state);
    result.steps = execution.steps;
    if (execution.status == OPTI_EXEC_ERROR) {
        result.error = execution.error;
        return result;
    }
    if ((returns_i32 && execution.status != OPTI_EXEC_RETURN_I32) ||
        (!returns_i32 && execution.status != OPTI_EXEC_RETURN_VOID)) {
        result.error = OPTI_EXEC_ERR_METHOD_RETURN;
        return result;
    }

    result.status = execution.status;
    result.error = OPTI_EXEC_ERR_NONE;
    result.int_value = execution.int_value;
    return result;
}

OptiExecResult execute_method(const MethodView &view, const char *name,
                              const char *descriptor, int32_t *locals,
                              size_t local_count, int32_t *stack,
                              size_t stack_capacity, uint32_t max_steps,
                              uint32_t depth, ExecutionState *state) {
    if ((view.file == nullptr && view.loaded == nullptr) || name == nullptr ||
        descriptor == nullptr || (locals == nullptr && local_count != 0) ||
        (stack == nullptr && stack_capacity != 0)) {
        return failed(OPTI_EXEC_ERR_ARGUMENT);
    }

    const uint8_t *code = nullptr;
    uint32_t code_length = 0;
    uint16_t max_stack = 0;
    uint16_t max_locals = 0;
    if (!method_code(view, name, descriptor, &code, &code_length, &max_stack,
                     &max_locals) || local_count < max_locals ||
        stack_capacity < max_stack) {
        return failed(OPTI_EXEC_ERR_ARGUMENT);
    }

    InvokeContext context{view, state, depth};
    return opticraft_bytecode_execute_i32_with_invoker(
        code, code_length, locals, local_count, stack, stack_capacity,
        max_steps, invoke_static, &context);
}
} // namespace

extern "C" OptiExecResult opticraft_method_execute_i32(
    const OptiClassFile *file, const char *name, const char *descriptor,
    int32_t *locals, size_t local_count, int32_t *stack,
    size_t stack_capacity, uint32_t max_steps) {
    return execute_method(MethodView{file, nullptr}, name, descriptor, locals,
                          local_count, stack, stack_capacity, max_steps, 0,
                          nullptr);
}

extern "C" OptiExecResult opticraft_loaded_method_execute_i32(
    const OptiLoadedClass *loaded, const char *name, const char *descriptor,
    int32_t *locals, size_t local_count, int32_t *stack,
    size_t stack_capacity, uint32_t max_steps) {
    if (loaded == nullptr) return failed(OPTI_EXEC_ERR_ARGUMENT);
    ExecutionState state;
    try {
        const char *name_in_file = nullptr;
        uint16_t name_length = 0;
        if (!opticraft_loaded_class_name(loaded, 0, &name_in_file,
                                         &name_length)) {
            return failed(OPTI_EXEC_ERR_ARGUMENT);
        }
        state.loaded_classes.emplace(std::string(name_in_file, name_length),
                                     loaded);
    } catch (...) {
        return failed(OPTI_EXEC_ERR_OUT_OF_MEMORY);
    }
    return execute_method(MethodView{nullptr, loaded}, name, descriptor,
                          locals, local_count, stack, stack_capacity,
                          max_steps, 0, &state);
}
