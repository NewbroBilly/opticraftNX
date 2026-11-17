#include "opticraft_bytecode.h"
#include <climits>
#include <cstdint>
namespace {
struct Frame { const uint8_t *code; size_t length,pc; int32_t *locals; size_t local_count; int32_t *stack; size_t stack_limit,sp; uint32_t steps; OptiExecResult result; OptiInvokeStaticI32 invoke_static; void *invoke_context; };
OptiExecResult fail(Frame &f,OptiExecError e){f.result.status=OPTI_EXEC_ERROR;f.result.error=e;f.result.steps=f.steps;f.result.pc=f.pc;return f.result;}
bool read_u1(Frame &f,uint8_t &v){if(f.pc>=f.length)return false;v=f.code[f.pc++];return true;}
bool read_u2(Frame &f,uint16_t &v){if(f.length-f.pc<2)return false;v=static_cast<uint16_t>((f.code[f.pc]<<8)|f.code[f.pc+1]);f.pc+=2;return true;}
bool read_s1(Frame &f,int32_t &v){uint8_t b;
   if(!read_u1(f,b))return false;v=static_cast<int8_t>(b);return true;}
bool read_s2(Frame &f,int32_t &v){if(f.length-f.pc<2)return false;uint16_t u=static_cast<uint16_t>((f.code[f.pc]<<8)|f.code[f.pc+1]);f.pc+=2;v=static_cast<int16_t>(u);return true;}
bool push(Frame &f,int32_t v){if(f.sp>=f.stack_limit)return false;f.stack[f.sp++]=v;return true;}
bool pop(Frame &f,int32_t &v){if(!f.sp)return false;v=f.stack[--f.sp];return true;}
bool branch(Frame &f,size_t origin,int32_t offset){int64_t t=static_cast<int64_t>(origin)+offset;
   if(t<0||t>=static_cast<int64_t>(f.length))return false;f.pc=static_cast<size_t>(t);return true;}
}
extern "C" OptiExecResult opticraft_bytecode_execute_i32_with_invoker(
 const uint8_t *code,size_t length,int32_t *locals,size_t local_count,
 int32_t *stack,size_t stack_limit,uint32_t max_steps,
 OptiInvokeStaticI32 invoke_static,void *context){
 Frame f{};f.code=code;f.length=length;f.locals=locals;f.local_count=local_count;f.stack=stack;f.stack_limit=stack_limit;f.invoke_static=invoke_static;f.invoke_context=context;f.result.status=OPTI_EXEC_ERROR;f.result.error=OPTI_EXEC_ERR_ARGUMENT;
 if((!code&&length)||(!locals&&local_count)||(!stack&&stack_limit)||!max_steps)return f.result;
 while(f.pc<f.length){
  if(f.steps>=max_steps)return fail(f,OPTI_EXEC_ERR_STEP_LIMIT);
  size_t origin=f.pc;uint8_t op=0;
   if(!read_u1(f,op))return fail(f,OPTI_EXEC_ERR_TRUNCATED);++f.steps;
  int32_t a=0,b=0,imm=0,offset=0;uint8_t index=0;
  switch(op){
  case 0x00:break;
  case 0x02:case 0x03:case 0x04:case 0x05:case 0x06:case 0x07:case 0x08:
   if(!push(f,static_cast<int32_t>(op)-3))return fail(f,OPTI_EXEC_ERR_STACK_OVERFLOW);
   break;
  case 0x10:if(!read_s1(f,imm))return fail(f,OPTI_EXEC_ERR_TRUNCATED);
   if(!push(f,imm))return fail(f,OPTI_EXEC_ERR_STACK_OVERFLOW);
   break;
  case 0x11:if(!read_s2(f,imm))return fail(f,OPTI_EXEC_ERR_TRUNCATED);
   if(!push(f,imm))return fail(f,OPTI_EXEC_ERR_STACK_OVERFLOW);
   break;
  case 0x15:case 0x36:
   if(!read_u1(f,index))return fail(f,OPTI_EXEC_ERR_TRUNCATED);
   if(index>=f.local_count)return fail(f,OPTI_EXEC_ERR_LOCAL_BOUNDS);
   if(op==0x15){if(!push(f,locals[index]))return fail(f,OPTI_EXEC_ERR_STACK_OVERFLOW);}
   else{if(!pop(f,a))return fail(f,OPTI_EXEC_ERR_STACK_UNDERFLOW);locals[index]=a;}
   break;
  case 0x1a:case 0x1b:case 0x1c:case 0x1d:
   index=static_cast<uint8_t>(op-0x1a);
   if(index>=f.local_count)return fail(f,OPTI_EXEC_ERR_LOCAL_BOUNDS);
   if(!push(f,locals[index]))return fail(f,OPTI_EXEC_ERR_STACK_OVERFLOW);
   break;
  case 0x3b:case 0x3c:case 0x3d:case 0x3e:
   index=static_cast<uint8_t>(op-0x3b);
   if(index>=f.local_count)return fail(f,OPTI_EXEC_ERR_LOCAL_BOUNDS);
   if(!pop(f,a))return fail(f,OPTI_EXEC_ERR_STACK_UNDERFLOW);
   locals[index]=a;
   break;
  case 0x57:if(!pop(f,a))return fail(f,OPTI_EXEC_ERR_STACK_UNDERFLOW);
   break;
  case 0x59:if(!f.sp)return fail(f,OPTI_EXEC_ERR_STACK_UNDERFLOW);
   if(!push(f,f.stack[f.sp-1]))return fail(f,OPTI_EXEC_ERR_STACK_OVERFLOW);
   break;
  case 0x60:case 0x64:case 0x68:case 0x6c:case 0x70:
   if(!pop(f,b)||!pop(f,a))return fail(f,OPTI_EXEC_ERR_STACK_UNDERFLOW);
   if(op==0x60)imm=static_cast<int32_t>(static_cast<uint32_t>(a)+static_cast<uint32_t>(b));
   else if(op==0x64)imm=static_cast<int32_t>(static_cast<uint32_t>(a)-static_cast<uint32_t>(b));
   else if(op==0x68)imm=static_cast<int32_t>(static_cast<uint32_t>(a)*static_cast<uint32_t>(b));
   else{if(!b)return fail(f,OPTI_EXEC_ERR_DIVIDE_BY_ZERO);
   if(a==INT32_MIN&&b==-1)imm=(op==0x6c)?INT32_MIN:0;else imm=(op==0x6c)?a/b:a%b;}
   if(!push(f,imm))return fail(f,OPTI_EXEC_ERR_STACK_OVERFLOW);
   break;
  case 0x74:if(!pop(f,a))return fail(f,OPTI_EXEC_ERR_STACK_UNDERFLOW);
   if(!push(f,static_cast<int32_t>(0u-static_cast<uint32_t>(a))))return fail(f,OPTI_EXEC_ERR_STACK_OVERFLOW);
   break;
  case 0x84:
   if(!read_u1(f,index)||!read_s1(f,imm))return fail(f,OPTI_EXEC_ERR_TRUNCATED);
   if(index>=f.local_count)return fail(f,OPTI_EXEC_ERR_LOCAL_BOUNDS);
   locals[index]=static_cast<int32_t>(static_cast<uint32_t>(locals[index])+static_cast<uint32_t>(imm));
   break;
  case 0x99:case 0x9a:case 0x9b:case 0x9c:case 0x9d:case 0x9e:
   if(!read_s2(f,offset))return fail(f,OPTI_EXEC_ERR_TRUNCATED);
   if(!pop(f,a))return fail(f,OPTI_EXEC_ERR_STACK_UNDERFLOW);
   if((op==0x99&&a==0)||(op==0x9a&&a!=0)||(op==0x9b&&a<0)||(op==0x9c&&a>=0)||(op==0x9d&&a>0)||(op==0x9e&&a<=0)){if(!branch(f,origin,offset))return fail(f,OPTI_EXEC_ERR_BRANCH_BOUNDS);}break;
  case 0x9f:case 0xa0:case 0xa1:case 0xa2:case 0xa3:case 0xa4:
   if(!read_s2(f,offset))return fail(f,OPTI_EXEC_ERR_TRUNCATED);
   if(!pop(f,b)||!pop(f,a))return fail(f,OPTI_EXEC_ERR_STACK_UNDERFLOW);
   if((op==0x9f&&a==b)||(op==0xa0&&a!=b)||(op==0xa1&&a<b)||(op==0xa2&&a>=b)||(op==0xa3&&a>b)||(op==0xa4&&a<=b)){if(!branch(f,origin,offset))return fail(f,OPTI_EXEC_ERR_BRANCH_BOUNDS);}break;
  case 0xa7:if(!read_s2(f,offset))return fail(f,OPTI_EXEC_ERR_TRUNCATED);
   if(!branch(f,origin,offset))return fail(f,OPTI_EXEC_ERR_BRANCH_BOUNDS);
   break;
  case 0xb8:{
   uint16_t method_ref=0;
   if(!read_u2(f,method_ref))return fail(f,OPTI_EXEC_ERR_TRUNCATED);
   if(!f.invoke_static)return fail(f,OPTI_EXEC_ERR_METHOD_REF);
   if(f.steps>=max_steps)return fail(f,OPTI_EXEC_ERR_STEP_LIMIT);
   OptiInvokeResult called=f.invoke_static(f.invoke_context,method_ref,
       f.stack,f.sp,max_steps-f.steps);
   const uint32_t remaining=max_steps-f.steps;
   if(called.steps>remaining){f.steps=max_steps;return fail(f,OPTI_EXEC_ERR_STEP_LIMIT);}
   f.steps+=called.steps;
   if(called.status==OPTI_EXEC_ERROR)
    return fail(f,called.error==OPTI_EXEC_ERR_NONE?OPTI_EXEC_ERR_METHOD_REF:called.error);
   if(called.argument_count>f.sp)return fail(f,OPTI_EXEC_ERR_STACK_UNDERFLOW);
   f.sp-=called.argument_count;
   if(called.status==OPTI_EXEC_RETURN_I32){
    if(!push(f,called.int_value))return fail(f,OPTI_EXEC_ERR_STACK_OVERFLOW);
   }else if(called.status!=OPTI_EXEC_RETURN_VOID)
    return fail(f,OPTI_EXEC_ERR_METHOD_RETURN);
   break;
  }
  case 0xac:if(!pop(f,a))return fail(f,OPTI_EXEC_ERR_STACK_UNDERFLOW);f.result.status=OPTI_EXEC_RETURN_I32;f.result.error=OPTI_EXEC_ERR_NONE;f.result.int_value=a;f.result.steps=f.steps;f.result.pc=f.pc;return f.result;
  case 0xb1:f.result.status=OPTI_EXEC_RETURN_VOID;f.result.error=OPTI_EXEC_ERR_NONE;f.result.steps=f.steps;f.result.pc=f.pc;return f.result;
  default:return fail(f,OPTI_EXEC_ERR_OPCODE);
  }
 }
 return fail(f,OPTI_EXEC_ERR_TRUNCATED);
}

extern "C" OptiExecResult opticraft_bytecode_execute_i32(
 const uint8_t *code,size_t length,int32_t *locals,size_t local_count,
 int32_t *stack,size_t stack_limit,uint32_t max_steps){
 return opticraft_bytecode_execute_i32_with_invoker(
     code,length,locals,local_count,stack,stack_limit,max_steps,nullptr,nullptr);
}
