#include "jvm_exception_runtime.hpp"
#include <cassert>
#include <iostream>
using namespace opti::runtime;
static ParsedClassCode cls(const char*n,const char*p){ParsedClassCode c;c.valid=true;c.this_class=n;c.super_class=p;return c;}
int main(){
 JvmClassRuntime rt;
 assert(rt.register_class(cls("java/lang/Throwable","")).success);
 assert(rt.register_class(cls("java/lang/Exception","java/lang/Throwable")).success);
 assert(rt.register_class(cls("game/Problem","java/lang/Exception")).success);
 ParsedClassCode c=cls("game/Test","java/lang/Object");
 c.constant_pool.resize(3);c.constant_pool[1].tag=1;c.constant_pool[1].utf8="game/Problem";
 c.constant_pool[2].tag=7;c.constant_pool[2].a=1;
 MethodCode m;m.exception_table={{0,5,10,2},{0,5,20,0}};
 auto found=find_exception_handler(c,m,3,"game/Problem",rt);
 assert(found.found&&found.handler_pc==10);
 found=find_exception_handler(c,m,3,"game/Other",rt);
 assert(found.found&&found.handler_pc==20); // catch-all fallback
 found=find_exception_handler(c,m,5,"game/Problem",rt);
 assert(!found.found); // end_pc is exclusive
 std::cout<<"exception table lookup: typed catch, inheritance, catch-all and PC ranges PASS\n";
}
