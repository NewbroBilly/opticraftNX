#include "jvm_object_interpreter.hpp"
#include <cassert>
#include <iostream>
using namespace opti::runtime;
namespace {
std::uint16_t utf(ParsedClassCode& c,const char* s){ConstantPoolEntry e;e.tag=1;e.utf8=s;c.constant_pool.push_back(e);return static_cast<std::uint16_t>(c.constant_pool.size()-1);}
std::uint16_t cls(ParsedClassCode& c,const char* s){auto n=utf(c,s);ConstantPoolEntry e;e.tag=7;e.a=n;c.constant_pool.push_back(e);return static_cast<std::uint16_t>(c.constant_pool.size()-1);}
std::uint16_t nat(ParsedClassCode& c,const char* n,const char* d){auto a=utf(c,n),b=utf(c,d);ConstantPoolEntry e;e.tag=12;e.a=a;e.b=b;c.constant_pool.push_back(e);return static_cast<std::uint16_t>(c.constant_pool.size()-1);}
std::uint16_t ref(ParsedClassCode& c,std::uint16_t owner,std::uint16_t nt){ConstantPoolEntry e;e.tag=10;e.a=owner;e.b=nt;c.constant_pool.push_back(e);return static_cast<std::uint16_t>(c.constant_pool.size()-1);}
}
int main(){
 ParsedClassCode c;c.valid=true;c.this_class="test/JavaLang";c.constant_pool.resize(1);
 const auto builder=cls(c,"java/lang/StringBuilder");
 const auto init=ref(c,builder,nat(c,"<init>","()V"));
 const auto append_string=ref(c,builder,nat(c,"append","(Ljava/lang/String;)Ljava/lang/StringBuilder;"));
 const auto append_long=ref(c,builder,nat(c,"append","(J)Ljava/lang/StringBuilder;"));
 const auto to_string=ref(c,builder,nat(c,"toString","()Ljava/lang/String;"));
 const auto string_utf=utf(c,"Switch ready");ConstantPoolEntry str;str.tag=8;str.a=string_utf;c.constant_pool.push_back(str);const auto string_index=static_cast<std::uint16_t>(c.constant_pool.size()-1);
 const auto system=cls(c,"java/lang/System");const auto now=ref(c,system,nat(c,"currentTimeMillis","()J"));
 ConstantPoolEntry lng;lng.tag=5;lng.long_integer=1000;c.constant_pool.push_back(lng);const auto long_index=static_cast<std::uint16_t>(c.constant_pool.size()-1);ConstantPoolEntry pad;c.constant_pool.push_back(pad);
 MethodCode m;m.access_flags=8;m.name="run";m.descriptor="()V";m.max_stack=4;m.max_locals=0;
 m.code={0xbb,static_cast<std::uint8_t>(builder>>8),static_cast<std::uint8_t>(builder),0x59,
 0xb7,static_cast<std::uint8_t>(init>>8),static_cast<std::uint8_t>(init),
 0x12,static_cast<std::uint8_t>(string_index),0xb6,static_cast<std::uint8_t>(append_string>>8),static_cast<std::uint8_t>(append_string),
 0xb8,static_cast<std::uint8_t>(now>>8),static_cast<std::uint8_t>(now),
 0x14,static_cast<std::uint8_t>(long_index>>8),static_cast<std::uint8_t>(long_index),0x71,
 0xb6,static_cast<std::uint8_t>(append_long>>8),static_cast<std::uint8_t>(append_long),
 0xb6,static_cast<std::uint8_t>(to_string>>8),static_cast<std::uint8_t>(to_string),0x57,0xb1};
 c.methods.push_back(m);
 JvmHeap heap;JvmStaticFields statics;ObjectBytecodeInterpreter vm(heap,statics);
 const auto result=vm.invoke_static(c,"run","()V",{});
 if(!result.success)std::cerr<<result.error<<"\n";
 assert(result.success);assert(result.instructions_executed==12);
 std::cout<<"java.lang intrinsics: StringBuilder, String constants, currentTimeMillis and long remainder PASS\n";
}
