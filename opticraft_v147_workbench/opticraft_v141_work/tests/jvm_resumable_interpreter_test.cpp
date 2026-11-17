#include "class_code_parser.hpp"
#include "jvm_resumable_interpreter.hpp"
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace opti::runtime;
int main(int argc,char**argv){
 assert(argc==2);std::ifstream in(argv[1],std::ios::binary);assert(in.good());
 std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
 auto cls=parse_class_code(bytes.data(),bytes.size());assert(cls.valid);
 ResumableIntegerInterpreter vm;
 assert(vm.begin(cls,"sum","(I)I",{100}));
 auto first=vm.step(7);assert(first.status==ResumableIntegerInterpreter::Status::Yielded);
 auto frames=vm.frames();assert(frames.size()==1&&frames[0].method_name=="sum"&&frames[0].pc>0);
 auto saved_pc=frames[0].pc;auto saved_locals=frames[0].locals;
 auto second=vm.step(11);assert(second.status==ResumableIntegerInterpreter::Status::Yielded);
 auto after=vm.frames();assert(after.size()==1&&after[0].pc!=saved_pc);
 // A fresh interpreter can independently execute while the first one is suspended.
 ResumableIntegerInterpreter other;assert(other.begin(cls,"branch","(I)I",{-9}));
 auto other_result=other.run_to_completion(3);assert(other_result.status==ResumableIntegerInterpreter::Status::Completed&&other_result.value==9);
 auto done=vm.run_to_completion(13);assert(done.status==ResumableIntegerInterpreter::Status::Completed&&done.value==5050);
 assert(done.instructions_executed>18);
 // Nested invokestatic creates a real second frame and resumes the caller after return.
 assert(vm.begin(cls,"factorial","(I)I",{6}));
 auto nested=vm.step(4);assert(nested.status==ResumableIntegerInterpreter::Status::Yielded);
 bool saw_nested=false;for(int i=0;i<20;++i){auto fs=vm.frames();if(fs.size()>1){saw_nested=true;break;}vm.step(1);}
 assert(saw_nested);
 auto fact=vm.run_to_completion(5);assert(fact.status==ResumableIntegerInterpreter::Status::Completed&&fact.value==720);
 assert(!vm.begin(cls,"missing","()I",{}));assert(vm.result().status==ResumableIntegerInterpreter::Status::Failed);
 std::cout<<"resumable integer interpreter PASS; sum(100)=5050 factorial(6)=720\n";
}
