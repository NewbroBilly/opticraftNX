#include "opticraft_method_executor.h"
#include <cassert>
#include <cstdint>
#include <vector>
int main(){
 // public class T { public static int mainCode() { return 5; } }
 const uint8_t bytes[]={
  0xca,0xfe,0xba,0xbe,0,0,0,49,0,8,
  1,0,1,'T',7,0,1,1,0,16,'j','a','v','a','/','l','a','n','g','/','O','b','j','e','c','t',7,0,3,
  1,0,4,'m','a','i','n',1,0,22,'(','[','L','j','a','v','a','/','l','a','n','g','/','S','t','r','i','n','g',';',')','V',
  1,0,4,'C','o','d','e',
  0,0x21,0,2,0,4,0,0,0,0,0,1,0,9,0,5,0,6,0,1,0,7,0,0,0,14,
  0,1,0,1,0,0,0,2,0x08,0xac,0,0,0,0
 };
 OptiClassFile *f=nullptr;assert(opticraft_classfile_parse(bytes,sizeof(bytes),&f));
 OptiClassSummary s{};assert(opticraft_classfile_summary(f,&s));assert(s.major_version==49&&s.methods_count==1);
 uint16_t flags=0;uint32_t code=99;assert(opticraft_classfile_find_method(f,"main","([Ljava/lang/String;)V",&flags,&code));assert(code==1);
 const uint8_t *method_code=nullptr;uint16_t max_stack=0,max_locals=0;
 assert(opticraft_classfile_method_code(f,"main","([Ljava/lang/String;)V",
   &method_code,&code,&max_stack,&max_locals));
 assert(code==2&&method_code[0]==0x08&&method_code[1]==0xac&&max_stack==1&&max_locals==1);
 int32_t locals[1]={0},stack[1]={0};
 auto executed=opticraft_method_execute_i32(f,"main","([Ljava/lang/String;)V",
   locals,1,stack,1,20);
 assert(executed.status==OPTI_EXEC_RETURN_I32&&executed.int_value==5);
 opticraft_classfile_destroy(f);assert(!opticraft_classfile_parse(bytes,8,&f));
 return 0;
}
