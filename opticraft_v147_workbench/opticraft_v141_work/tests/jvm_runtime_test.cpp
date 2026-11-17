#include "jvm_runtime.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <variant>
using namespace opti::runtime;
int main(){
 JvmHeap heap(32);
 std::vector<FieldLayout> layout={{"value","I",false},{"ratio","F",false},{"child","Lgame/Node;",false},{"STATIC","I",true}};
 auto a=heap.allocate_instance("game/Node",layout),b=heap.allocate_instance("game/Node",layout),dead=heap.allocate_object("game/Unused");
 JvmValue v;
 assert(a&&b&&dead&&heap.valid_ref(a));
 assert(heap.put_field(a,"next",ObjectRef(b)));
 assert(heap.get_field(a,"value",v)&&std::get<std::int32_t>(v)==0);
 assert(!heap.get_field(a,"STATIC",v));
 assert(heap.put_field(b,"next",ObjectRef(a)));
 assert(heap.put_field(a,"value",std::int32_t(42)));
 assert(heap.get_field(a,"value",v)&&std::get<std::int32_t>(v)==42);
 auto ints=heap.allocate_int_array(4);assert(ints);
 assert(heap.array_store(ints,2,std::int32_t(77)));
 assert(heap.array_load(ints,2,v)&&std::get<std::int32_t>(v)==77);
 assert(!heap.array_store(ints,0,ObjectRef(a)));
 auto refs=heap.allocate_reference_array(2,"game/Node");assert(refs);
 assert(heap.array_store(refs,0,ObjectRef(a)));
 assert(!heap.array_store(refs,1,std::int32_t(2)));
 JvmStaticFields statics;assert(statics.define("game/Main","ROOT",ObjectRef(refs)));
 assert(!statics.define("game/Main","ROOT",NullValue{}));
 auto removed=heap.collect_garbage(statics.roots());
 assert(removed==2&&!heap.valid_ref(dead)&&!heap.valid_ref(ints));
 assert(heap.valid_ref(a)&&heap.valid_ref(b)&&heap.valid_ref(refs)&&heap.valid_ref(ints)==false);
 // Arrays not reachable from static root except refs -> a,b. The int array must be collected.
 assert(heap.get_field(b,"next",v)&&std::get<ObjectRef>(v)==a);
 auto stats=heap.stats();assert(stats.live_objects==3&&stats.collected_objects==2);
 assert(statics.set("game/Main","ROOT",NullValue{}));
 removed=heap.collect_garbage(statics.roots());assert(removed==3&&heap.stats().live_objects==0);
 assert(heap.allocate_object("game/Again")!=0);
 JvmHeap tiny(1);assert(tiny.allocate_object("One")!=0);assert(tiny.allocate_object("Two")==0);
 std::cout<<"JVM heap/object/array/static-root/mark-sweep tests passed\n";
}
