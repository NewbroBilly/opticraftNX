#include "jvm_interpreter.hpp"
#include <cstring>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>
namespace opti::runtime {
namespace {
std::uint16_t read_u2(const std::vector<std::uint8_t>&c,std::size_t p){return (std::uint16_t(c[p])<<8)|c[p+1];}
std::int16_t read_s2(const std::vector<std::uint8_t>&c,std::size_t p){return static_cast<std::int16_t>(read_u2(c,p));}
std::int32_t read_s4(const std::vector<std::uint8_t>&c,std::size_t p){const std::uint32_t u=(std::uint32_t(c[p])<<24)|(std::uint32_t(c[p+1])<<16)|(std::uint32_t(c[p+2])<<8)|c[p+3];std::int32_t v;std::memcpy(&v,&u,sizeof(v));return v;}
std::int32_t wrap32(std::uint32_t v){std::int32_t out;std::memcpy(&out,&v,sizeof(out));return out;}
bool parse_int_descriptor(const std::string& d,std::size_t& argc){
 argc=0;if(d.empty()||d[0]!='(')return false;std::size_t p=1;
 while(p<d.size()&&d[p]!=')'){if(d[p++]!='I')return false;++argc;}
 return p+2==d.size()&&d[p]==')'&&d[p+1]=='I';
}
struct SharedBudget {std::size_t remaining=0,total=0;};
ExecutionResult run_method(const ParsedClassCode&cls,const std::string&name,const std::string&desc,
                           const std::vector<std::int32_t>&args,SharedBudget&budget,std::size_t depth){
 ExecutionResult out;auto fail=[&](const std::string&m){out.error=m;return out;};
 if(depth>64)return fail("maximum Java call depth exceeded");
 if(!cls.valid)return fail("classfile was not parsed successfully");
 const MethodCode* method=nullptr;for(const auto&m:cls.methods)if(m.name==name&&m.descriptor==desc){method=&m;break;}
 if(!method)return fail("method not found: "+name+desc);
 if((method->access_flags&0x0008)==0)return fail("only static methods are supported");
 std::size_t expected_args=0;if(!parse_int_descriptor(desc,expected_args))return fail("only methods with integer parameters and integer return are supported");
 if(args.size()!=expected_args)return fail("argument count does not match method descriptor");
 if(args.size()>method->max_locals)return fail("arguments exceed max_locals");
 if(method->code.empty())return fail("method has no Code body");
 std::vector<std::int32_t> locals(method->max_locals,0),stack;stack.reserve(method->max_stack);
 for(std::size_t i=0;i<args.size();i++)locals[i]=args[i];
 std::size_t pc=0;const std::size_t instructions_before=budget.total;
 auto push=[&](std::int32_t v)->bool{if(stack.size()>=method->max_stack)return false;stack.push_back(v);return true;};
 auto pop=[&](std::int32_t&v)->bool{if(stack.empty())return false;v=stack.back();stack.pop_back();return true;};
 auto local=[&](std::size_t i)->bool{return i<locals.size();};
 while(pc<method->code.size()){
  if(budget.remaining==0)return fail("instruction budget exceeded");
  --budget.remaining;++budget.total;
  const std::size_t start=pc;const std::uint8_t op=method->code[pc++];
  auto need=[&](std::size_t n){return pc<=method->code.size()&&n<=method->code.size()-pc;};
  auto load=[&](std::size_t idx)->bool{return local(idx)&&push(locals[idx]);};
  auto store=[&](std::size_t idx)->bool{std::int32_t v;if(!local(idx)||!pop(v))return false;locals[idx]=v;return true;};
  auto branch=[&](std::int32_t delta)->bool{const std::int64_t target=std::int64_t(start)+delta;if(target<0||target>=std::int64_t(method->code.size()))return false;pc=static_cast<std::size_t>(target);return true;};
  std::int32_t a=0,b=0;std::size_t idx=0;
  if(op==0x00)continue;
  if(op>=0x02&&op<=0x08){if(!push(static_cast<std::int32_t>(op)-3))return fail("operand stack overflow");continue;}
  if(op==0x10){if(!need(1))return fail("truncated bipush");auto v=static_cast<std::int8_t>(method->code[pc++]);if(!push(v))return fail("operand stack overflow");continue;}
  if(op==0x11){if(!need(2))return fail("truncated sipush");auto v=read_s2(method->code,pc);pc+=2;if(!push(v))return fail("operand stack overflow");continue;}
  if(op==0x12||op==0x13){std::uint16_t cp_index=0;if(op==0x12){if(!need(1))return fail("truncated ldc");cp_index=method->code[pc++];}else{if(!need(2))return fail("truncated ldc_w");cp_index=read_u2(method->code,pc);pc+=2;}if(cp_index==0||cp_index>=cls.constant_pool.size()||cls.constant_pool[cp_index].tag!=3)return fail("ldc currently supports integer constants only");if(!push(cls.constant_pool[cp_index].integer))return fail("operand stack overflow");continue;}
  if(op==0xc4){if(!need(1))return fail("truncated wide");const auto sub=method->code[pc++];if(!need(2))return fail("truncated wide index");idx=read_u2(method->code,pc);pc+=2;if(sub==0x84){if(!need(2)||!local(idx))return fail("invalid wide iinc");auto amount=read_s2(method->code,pc);pc+=2;locals[idx]=wrap32(std::uint32_t(locals[idx])+static_cast<std::uint32_t>(static_cast<std::int32_t>(amount)));continue;}if(sub==0x15){if(!load(idx))return fail("invalid wide iload");continue;}if(sub==0x36){if(!store(idx))return fail("invalid wide istore");continue;}return fail("unsupported wide sub-opcode");}
  if(op==0xaa||op==0xab){
   while(pc%4){if(!need(1))return fail("truncated switch padding");++pc;}
   if(!need(op==0xaa?12:8))return fail("truncated switch header");
   const auto def=read_s4(method->code,pc);pc+=4;std::int32_t delta=def;
   if(op==0xaa){auto low=read_s4(method->code,pc),high=read_s4(method->code,pc+4);pc+=8;if(high<low||std::int64_t(high)-low>16380)return fail("invalid tableswitch range");if(!pop(a))return fail("stack underflow: tableswitch");const auto count=std::size_t(std::int64_t(high)-low+1);if(!need(count*4))return fail("truncated tableswitch offsets");if(a>=low&&a<=high)delta=read_s4(method->code,pc+std::size_t(a-low)*4);pc+=count*4;}
   else{auto pairs=read_s4(method->code,pc);pc+=4;if(pairs<0||pairs>8191||!need(std::size_t(pairs)*8))return fail("invalid lookupswitch pairs");if(!pop(a))return fail("stack underflow: lookupswitch");std::int32_t prev=INT32_MIN;for(std::int32_t i=0;i<pairs;i++){auto key=read_s4(method->code,pc),off=read_s4(method->code,pc+4);pc+=8;if(i&&key<=prev)return fail("lookupswitch keys not ordered");prev=key;if(key==a)delta=off;}}
   if(!branch(delta))return fail("invalid switch target");
   continue;
  }
  if(op>=0x15&&op<=0x19){if(!need(1))return fail("truncated load");idx=method->code[pc++];if(op!=0x15)return fail("only integer local loads are supported");if(!load(idx))return fail("invalid local load or stack overflow");continue;}
  if(op>=0x1a&&op<=0x1d){idx=op-0x1a;if(!load(idx))return fail("invalid iload_n");continue;}
  if(op==0x36){if(!need(1))return fail("truncated istore");idx=method->code[pc++];if(!store(idx))return fail("invalid istore");continue;}
  if(op>=0x3b&&op<=0x3e){idx=op-0x3b;if(!store(idx))return fail("invalid istore_n");continue;}
  if(op==0x84){if(!need(2))return fail("truncated iinc");idx=method->code[pc++];auto amount=static_cast<std::int8_t>(method->code[pc++]);if(!local(idx))return fail("iinc local index out of range");locals[idx]=wrap32(std::uint32_t(locals[idx])+static_cast<std::uint32_t>(static_cast<std::int32_t>(amount)));continue;}
  if(op==0x57){if(!pop(a))return fail("stack underflow: pop");continue;}
  if(op==0x59){if(stack.empty()||!push(stack.back()))return fail("stack underflow/overflow: dup");continue;}
  if(op==0x60||op==0x64){if(!pop(b)||!pop(a))return fail("stack underflow: integer arithmetic");auto v=op==0x60?wrap32(std::uint32_t(a)+std::uint32_t(b)):wrap32(std::uint32_t(a)-std::uint32_t(b));if(!push(v))return fail("operand stack overflow");continue;}
  if(op==0x68||op==0x6c||op==0x70||op==0x7e||op==0x80||op==0x82||op==0x78||op==0x7a||op==0x7c){if(!pop(b)||!pop(a))return fail("stack underflow: integer operation");std::int32_t v=0;switch(op){case 0x68:v=wrap32(std::uint32_t(a)*std::uint32_t(b));break;case 0x6c:if(b==0)return fail("integer division by zero");v=(a==INT32_MIN&&b==-1)?INT32_MIN:a/b;break;case 0x70:if(b==0)return fail("integer remainder by zero");v=(a==INT32_MIN&&b==-1)?0:a%b;break;case 0x7e:v=a&b;break;case 0x80:v=a|b;break;case 0x82:v=a^b;break;case 0x78:v=wrap32(std::uint32_t(a)<<(b&31));break;case 0x7a:v=a>>(b&31);break;case 0x7c:v=static_cast<std::int32_t>(std::uint32_t(a)>>(b&31));break;}if(!push(v))return fail("operand stack overflow");continue;}
  if(op==0x74){if(!pop(a)||!push(wrap32(0u-std::uint32_t(a))))return fail("stack underflow/overflow: ineg");continue;}
  if(op==0x91||op==0x92||op==0x93){if(!pop(a))return fail("stack underflow: integer narrowing");if(op==0x91)a=static_cast<std::int8_t>(a);else if(op==0x92)a=static_cast<std::uint16_t>(a);else a=static_cast<std::int16_t>(a);if(!push(a))return fail("operand stack overflow");continue;}
  if(op>=0x99&&op<=0x9e){if(!need(2))return fail("truncated conditional branch");auto delta=read_s2(method->code,pc);pc+=2;if(!pop(a))return fail("stack underflow: if");bool take=false;switch(op){case 0x99:take=a==0;break;case 0x9a:take=a!=0;break;case 0x9b:take=a<0;break;case 0x9c:take=a>=0;break;case 0x9d:take=a>0;break;case 0x9e:take=a<=0;break;}if(take&&!branch(delta))return fail("invalid conditional branch target");continue;}
  if(op>=0x9f&&op<=0xa4){if(!need(2))return fail("truncated integer comparison branch");auto delta=read_s2(method->code,pc);pc+=2;if(!pop(b)||!pop(a))return fail("stack underflow: integer comparison");bool take=false;switch(op){case 0x9f:take=a==b;break;case 0xa0:take=a!=b;break;case 0xa1:take=a<b;break;case 0xa2:take=a>=b;break;case 0xa3:take=a>b;break;case 0xa4:take=a<=b;break;}if(take&&!branch(delta))return fail("invalid comparison branch target");continue;}
  if(op==0xa7){if(!need(2))return fail("truncated goto");auto delta=read_s2(method->code,pc);pc+=2;if(!branch(delta))return fail("invalid goto target");continue;}
  if(op==0xc8){if(!need(4))return fail("truncated goto_w");auto delta=read_s4(method->code,pc);pc+=4;if(!branch(delta))return fail("invalid goto_w target");continue;}
  if(op==0xb8){
   if(!need(2))return fail("truncated invokestatic");
   auto cp_index=read_u2(method->code,pc);pc+=2;
   if(cp_index==0||cp_index>=cls.constant_pool.size()||(cls.constant_pool[cp_index].tag!=10&&cls.constant_pool[cp_index].tag!=11))return fail("invalid invokestatic constant-pool entry");
   const auto& ref=cls.constant_pool[cp_index];if(ref.a==0||ref.a>=cls.constant_pool.size()||ref.b==0||ref.b>=cls.constant_pool.size())return fail("invalid method reference indices");
   const auto& class_entry=cls.constant_pool[ref.a];const auto& nt=cls.constant_pool[ref.b];
   if(class_entry.tag!=7||nt.tag!=12)return fail("invalid method reference structure");
   const auto& owner=cls.constant_pool[class_entry.a].utf8;const auto& target_name=cls.constant_pool[nt.a].utf8;const auto& target_desc=cls.constant_pool[nt.b].utf8;
   if(owner!=cls.this_class)return fail("cross-class invocation is not supported yet: "+owner);
   std::size_t count=0;if(!parse_int_descriptor(target_desc,count))return fail("invokestatic target descriptor is not integer-only");
   std::vector<std::int32_t> call_args(count);for(std::size_t i=count;i>0;i--)if(!pop(call_args[i-1]))return fail("stack underflow: invokestatic arguments");
   auto called=run_method(cls,target_name,target_desc,call_args,budget,depth+1);if(!called.success)return fail("invokestatic "+target_name+": "+called.error);
   if(!push(called.int_value))return fail("operand stack overflow after invokestatic");
   continue;
  }
  if(op==0xac){if(!pop(out.int_value))return fail("stack underflow: ireturn");out.success=true;out.instructions_executed=budget.total-instructions_before;return out;}
  if(op==0xb1)return fail("void return used in integer method");
  return fail("unsupported opcode "+std::to_string(op)+" at bytecode offset "+std::to_string(start));
 }
 return fail("method terminated without return");
}
}
ExecutionResult IntegerBytecodeInterpreter::invoke_static_int(const ParsedClassCode&cls,const std::string&method,
 const std::string&descriptor,const std::vector<std::int32_t>&args)const{
 SharedBudget budget{budget_,0};return run_method(cls,method,descriptor,args,budget,0);
}
}
