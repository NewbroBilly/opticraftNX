#include "opticraft_method_executor.h"
extern "C" OptiExecResult opticraft_method_execute_i32(
 const OptiClassFile *file,const char *name,const char *descriptor,
 int32_t *locals,size_t local_count,int32_t *stack,size_t stack_capacity,
 uint32_t max_steps) {
 const uint8_t *code=nullptr;uint32_t code_length=0;uint16_t max_stack=0,max_locals=0;
 if(!file||!name||!descriptor||!opticraft_classfile_method_code(
   file,name,descriptor,&code,&code_length,&max_stack,&max_locals)||
   local_count<max_locals||stack_capacity<max_stack) {
  OptiExecResult failed{};failed.status=OPTI_EXEC_ERROR;failed.error=OPTI_EXEC_ERR_ARGUMENT;return failed;
 }
 return opticraft_bytecode_execute_i32(code,code_length,locals,local_count,
                                       stack,stack_capacity,max_steps);
}
