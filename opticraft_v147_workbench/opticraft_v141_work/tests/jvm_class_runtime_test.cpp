#include "jvm_class_runtime.hpp"
#include <cassert>
#include <iostream>
using namespace opti::runtime;
static ParsedClassCode make(const char*n,const char*parent,int flags=0){
 ParsedClassCode c;c.valid=true;c.this_class=n;c.super_class=parent;
 c.methods.push_back({static_cast<std::uint16_t>(flags),"value","()I",1,1,{0x07,0xac},{}});
 c.fields.push_back({0,"x","I"});return c;
}
int main(){
 JvmClassRuntime rt;
 auto root_class=make("java/lang/Object","");root_class.fields.clear();assert(rt.register_class(root_class).success);
 assert(rt.register_class(make("Base","java/lang/Object")).success);
 assert(rt.register_class(make("Child","Base")).success);
 assert(rt.register_class(make("Other","java/lang/Object")).success);
 assert(!rt.register_class(make("Child","Base")).success);
 auto r=rt.resolve_method("Base","value","()I",InvokeKind::Virtual,"Child");
 assert(r.method&&r.owner_name=="Child");
 r=rt.resolve_method("Base","value","()I",InvokeKind::Special);
 assert(r.method&&r.owner_name=="Base");
 assert(rt.is_assignable("Child","Base"));
 assert(rt.is_assignable("Child","java/lang/Object"));
 assert(!rt.is_assignable("Other","Base"));
 std::vector<FieldLayout> layout;std::string error;
 assert(rt.build_instance_layout("Child",layout,error)&&layout.size()==2);
 assert(layout[0].name=="Base.x"&&layout[1].name=="Child.x");
 JvmHeap heap;auto obj=heap.allocate_instance("Child",layout);assert(obj);
 assert(rt.instance_of(heap,obj,"Base")&&rt.instance_of(heap,obj,"java/lang/Object"));
 assert(!rt.instance_of(heap,obj,"Other"));
 auto cyc=make("CycleA","CycleB");auto cyc2=make("CycleB","CycleA");
 assert(rt.register_class(cyc).success&&rt.register_class(cyc2).success);
 assert(!rt.build_instance_layout("CycleA",layout,error));
 // VM-wide class initialization ownership and erroneous-state semantics.
 assert(rt.begin_class_initialization("Base",11,error));
 assert(rt.class_initialization("Base").state==ClassInitializationState::Initializing);
 assert(!rt.begin_class_initialization("Base",22,error));
 assert(rt.begin_class_initialization("Base",11,error)); // recursive access by owner
 assert(rt.complete_class_initialization("Base",11));
 assert(rt.class_initialization("Base").state==ClassInitializationState::Initialized);
 assert(rt.begin_class_initialization("Child",33,error));
 assert(rt.fail_class_initialization("Child",33,"boom"));
 assert(rt.class_initialization("Child").state==ClassInitializationState::Erroneous);
 assert(!rt.begin_class_initialization("Child",44,error));
 assert(error.find("NoClassDefFoundError")!=std::string::npos);
 std::cout<<"class runtime: registration, inheritance, dispatch, assignability and field layouts PASS\n";
}
