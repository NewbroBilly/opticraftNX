#ifndef OPTICRAFT_METHOD_EXECUTOR_H
#define OPTICRAFT_METHOD_EXECUTOR_H
#include "opticraft_bytecode.h"
#include "opticraft_classfile.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Executes a method's verified Code attribute using the bounded i32 engine.
 * The classfile owns the borrowed bytecode pointer for the duration of call. */
OptiExecResult opticraft_method_execute_i32(const OptiClassFile *file,
 const char *name,const char *descriptor,int32_t *locals,size_t local_count,
 int32_t *stack,size_t stack_capacity,uint32_t max_steps);
#ifdef __cplusplus
}
#endif
#endif
