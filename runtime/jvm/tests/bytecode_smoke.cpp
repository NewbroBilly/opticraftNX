#include "opticraft_bytecode.h"
#include <cassert>
#include <cstdint>
int main(){
 int32_t locals[4]={0,0,0,0},stack[16]={0};
 const uint8_t arithmetic[]={0x10,7,0x3b,0x1a,0x10,6,0x68,0x10,2,0x64,0xac};
 auto r=opticraft_bytecode_execute_i32(arithmetic,sizeof(arithmetic),locals,4,stack,16,100);
 assert(r.status==OPTI_EXEC_RETURN_I32&&r.int_value==40&&r.error==OPTI_EXEC_ERR_NONE);
 const uint8_t branch_code[]={0x03,0x03,0x9f,0x00,0x06,0x10,3,0xac,0x10,9,0xac};
 r=opticraft_bytecode_execute_i32(branch_code,sizeof(branch_code),locals,4,stack,16,100);
 assert(r.status==OPTI_EXEC_RETURN_I32&&r.int_value==9);
 const uint8_t divide_zero[]={0x10,8,0x03,0x6c,0xac};
 r=opticraft_bytecode_execute_i32(divide_zero,sizeof(divide_zero),locals,4,stack,16,100);
 assert(r.status==OPTI_EXEC_ERROR&&r.error==OPTI_EXEC_ERR_DIVIDE_BY_ZERO);
 const uint8_t unsupported[]={0xb8,0,0,0xb1};
 r=opticraft_bytecode_execute_i32(unsupported,sizeof(unsupported),locals,4,stack,16,100);
 assert(r.status==OPTI_EXEC_ERROR&&r.error==OPTI_EXEC_ERR_METHOD_REF);
 const uint8_t loop[]={0x84,0,1,0x1a,0x10,4,0xa1,0xff,0xfa,0x1a,0xac};
 locals[0]=0;r=opticraft_bytecode_execute_i32(loop,sizeof(loop),locals,4,stack,16,100);
 assert(r.status==OPTI_EXEC_RETURN_I32&&r.int_value==4);
 return 0;
}
