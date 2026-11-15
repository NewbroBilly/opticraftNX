#ifndef OPTICRAFT_BYTECODE_H
#define OPTICRAFT_BYTECODE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum OptiExecStatus { OPTI_EXEC_ERROR=0, OPTI_EXEC_RETURN_VOID=1, OPTI_EXEC_RETURN_I32=2 } OptiExecStatus;
typedef enum OptiExecError { OPTI_EXEC_ERR_NONE=0, OPTI_EXEC_ERR_ARGUMENT=1, OPTI_EXEC_ERR_OPCODE=2, OPTI_EXEC_ERR_TRUNCATED=3, OPTI_EXEC_ERR_STACK_UNDERFLOW=4, OPTI_EXEC_ERR_STACK_OVERFLOW=5, OPTI_EXEC_ERR_LOCAL_BOUNDS=6, OPTI_EXEC_ERR_DIVIDE_BY_ZERO=7, OPTI_EXEC_ERR_BRANCH_BOUNDS=8, OPTI_EXEC_ERR_STEP_LIMIT=9 } OptiExecError;
typedef struct OptiExecResult { OptiExecStatus status; OptiExecError error; int32_t int_value; uint32_t steps; size_t pc; } OptiExecResult;
/* Bounded Java 1-slot integer bytecode subset. References, floating point,
 * invocation, fields, monitors and exception tables are not implemented yet.
 * Unsupported opcodes fail closed. */
OptiExecResult opticraft_bytecode_execute_i32(const uint8_t *code,size_t code_length,int32_t *locals,size_t max_locals,int32_t *stack,size_t max_stack,uint32_t max_steps);
#ifdef __cplusplus
}
#endif
#endif
