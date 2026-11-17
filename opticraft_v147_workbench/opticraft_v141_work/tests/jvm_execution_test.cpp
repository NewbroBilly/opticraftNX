#include "class_code_parser.hpp"
#include "jvm_interpreter.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace opti::runtime;
int main(int argc,char**argv){
 assert(argc==2);std::ifstream in(argv[1],std::ios::binary);assert(in.good());
 std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
 auto cls=parse_class_code(bytes.data(),bytes.size());
 if(!cls.valid){std::cerr<<cls.error<<"\n";return 2;}
 assert(cls.this_class=="VmFixture");
 assert(cls.fields.size()==2);
 const MethodCode* sum=nullptr;for(const auto&m:cls.methods)if(m.name=="sum"&&m.descriptor=="(I)I")sum=&m;
 assert(sum&&sum->code.size()>0&&sum->max_locals>=3&&sum->max_stack>=2);
 IntegerBytecodeInterpreter vm;
 auto r=vm.invoke_static_int(cls,"sum","(I)I",{10});
 if(!r.success){std::cerr<<r.error<<"\n";return 3;}
 assert(r.int_value==55);
 auto r2=vm.invoke_static_int(cls,"sum","(I)I",{100});assert(r2.success&&r2.int_value==5050);
 auto r3=vm.invoke_static_int(cls,"sum","(I)I",{0});assert(r3.success&&r3.int_value==0);
 auto branch=vm.invoke_static_int(cls,"branch","(I)I",{-7});
 if(!branch.success){std::cerr<<branch.error<<"\n";return 4;}
 assert(branch.int_value==7);
 auto branch2=vm.invoke_static_int(cls,"branch","(I)I",{7});assert(branch2.success&&branch2.int_value==9);
 auto choice=vm.invoke_static_int(cls,"choose","(I)I",{2});assert(choice.success&&choice.int_value==30);
 auto choice2=vm.invoke_static_int(cls,"choose","(I)I",{99});assert(choice2.success&&choice2.int_value==-5);
 auto constant=vm.invoke_static_int(cls,"constant","()I",{});assert(constant.success&&constant.int_value==1234567);
 const MethodCode* guarded=nullptr;for(const auto&m:cls.methods)if(m.name=="guarded")guarded=&m;
 assert(guarded && !guarded->exception_table.empty());
 auto sparse=vm.invoke_static_int(cls,"sparseSwitch","(I)I",{100});assert(sparse.success&&sparse.int_value==12);
 auto fact=vm.invoke_static_int(cls,"factorial","(I)I",{6});
 if(!fact.success){std::cerr<<fact.error<<"\\n";return 5;} assert(fact.int_value==720);
 auto wide=vm.invoke_static_int(cls,"wideLocal","()I",{});
 if(!wide.success){std::cerr<<wide.error<<"\\n";return 6;} assert(wide.int_value==259);
 auto divzero=vm.invoke_static_int(cls,"divide","(II)I",{10,0});assert(!divzero.success);
 assert(!vm.invoke_static_int(cls,"sum","(I)I",{}).success);
 assert(!vm.invoke_static_int(cls,"missing","(I)I",{1}).success);
 IntegerBytecodeInterpreter limited(5);
 assert(!limited.invoke_static_int(cls,"sum","(I)I",{100}).success);
 std::cout<<"class Code parser + interpreter PASS; methods="<<cls.methods.size()
          <<" sum(100)="<<r2.int_value<<" sum_instructions="<<r2.instructions_executed<<"\n";
}
