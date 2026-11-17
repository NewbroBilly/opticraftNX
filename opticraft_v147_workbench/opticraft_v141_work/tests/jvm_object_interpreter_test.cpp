#include "jvm_object_interpreter.hpp"
#include <cassert>
#include <iostream>
using namespace opti::runtime;
int main(){
 ParsedClassCode c;c.valid=true;c.this_class="Fixture";
 auto utf=[](const char*s){ConstantPoolEntry e;e.tag=1;e.utf8=s;return e;};
 c.constant_pool.resize(11);
 c.constant_pool[1]=utf("Fixture");
 c.constant_pool[2].tag=7;c.constant_pool[2].a=1;
 c.constant_pool[3]=utf("value");c.constant_pool[4]=utf("I");
 c.constant_pool[5].tag=12;c.constant_pool[5].a=3;c.constant_pool[5].b=4;
 c.constant_pool[6]=utf("<init>");c.constant_pool[7]=utf("()V");
 c.constant_pool[8].tag=12;c.constant_pool[8].a=6;c.constant_pool[8].b=7;
 c.constant_pool[9].tag=10;c.constant_pool[9].a=2;c.constant_pool[9].b=8;
 c.constant_pool[10].tag=9;c.constant_pool[10].a=2;c.constant_pool[10].b=5;
 c.fields.push_back({0,"value","I"});
 MethodCode m;m.access_flags=8;m.name="run";m.descriptor="()I";m.max_stack=3;m.max_locals=0;
 m.code={0xbb,0,2,0x59,0xb7,0,9,0x59,0x10,7,0xb5,0,10,0xb4,0,10,0xac};
 c.methods.push_back(m);
 JvmHeap heap;JvmStaticFields statics;ObjectBytecodeInterpreter vm(heap,statics);
 auto result=vm.invoke_static(c,"run","()I",{});
 assert(result.success&&std::get<std::int32_t>(result.value)==7);
 assert(result.instructions_executed==8);
 const auto& completed_frames = vm.execution_frames().completed_frames();
 assert(completed_frames.size() == 1);
 assert(completed_frames[0].class_name == c.this_class);
 assert(completed_frames[0].method_name == "run");
 assert(completed_frames[0].instruction_epoch >= 1);
 // Arrays: int[] a=new int[3]; a[1]=42; return a[1];
 ParsedClassCode arr=c;arr.methods.clear();arr.fields.clear();
 MethodCode am;am.access_flags=8;am.name="array";am.descriptor="()I";am.max_stack=4;am.max_locals=0;
 am.code={0x06,0xbc,10,0x59,0x04,0x10,42,0x4f,0x04,0x2e,0xac};
 arr.methods.push_back(am);
 auto ar=vm.invoke_static(arr,"array","()I",{});
 assert(ar.success&&std::get<std::int32_t>(ar.value)==42);
 // wide local-variable instructions address slots above 255.
 ParsedClassCode wide=c;wide.methods.clear();wide.fields.clear();
 MethodCode wm;wm.access_flags=8;wm.name="wideLocals";wm.descriptor="()I";wm.max_stack=1;wm.max_locals=261;
 wm.code={0x08,0xc4,0x36,0x01,0x04,0xc4,0x84,0x01,0x04,0x00,0x02,0xc4,0x15,0x01,0x04,0xac};
 wide.methods.push_back(wm);
 auto wr=vm.invoke_static(wide,"wideLocals","()I",{});
 assert(wr.success&&std::get<std::int32_t>(wr.value)==7);

 // dup_x1 and swap preserve JVM operand-stack order.
 ParsedClassCode stack_ops=c;stack_ops.methods.clear();stack_ops.fields.clear();
 MethodCode sm;sm.access_flags=8;sm.name="stackOps";sm.descriptor="()I";sm.max_stack=3;sm.max_locals=0;
 sm.code={0x04,0x05,0x5a,0x57,0x5f,0x57,0xac};
 stack_ops.methods.push_back(sm);
 auto sr=vm.invoke_static(stack_ops,"stackOps","()I",{});
 assert(sr.success&&std::get<std::int32_t>(sr.value)==1);

 // instanceof checks the runtime class of an allocated object.
 ParsedClassCode typecheck=c;typecheck.methods.clear();typecheck.fields.clear();
 MethodCode tm;tm.access_flags=8;tm.name="isFixture";tm.descriptor="()I";tm.max_stack=2;tm.max_locals=0;
 tm.code={0xbb,0,2,0x59,0xb7,0,9,0xc1,0,2,0xac};
 typecheck.methods.push_back(tm);
 auto tr=vm.invoke_static(typecheck,"isFixture","()I",{});
 assert(tr.success&&std::get<std::int32_t>(tr.value)==1);

 // athrow transfers control to an in-method catch-all handler.
 ParsedClassCode caught=c;caught.methods.clear();caught.fields.clear();
 MethodCode em;em.access_flags=8;em.name="catchAll";em.descriptor="()I";em.max_stack=2;em.max_locals=0;
 em.code={0xbb,0,2,0x59,0xb7,0,9,0xbf,0x57,0x08,0xac};
 em.exception_table.push_back({0,8,8,0});
 caught.methods.push_back(em);
 auto er=vm.invoke_static(caught,"catchAll","()I",{});
 assert(er.success&&std::get<std::int32_t>(er.value)==5);
 std::cout<<"object bytecode interpreter: objects, arrays, wide locals, stack ops, instanceof and athrow handler PASS\n";
}
