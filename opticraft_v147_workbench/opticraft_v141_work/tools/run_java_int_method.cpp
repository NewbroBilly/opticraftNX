#include "class_code_parser.hpp"
#include "jvm_interpreter.hpp"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>
int main(int argc,char**argv){
 if(argc<4){std::cerr<<"usage: run_java_int_method <classfile> <method> <descriptor> [int args...]\n";return 2;}
 std::ifstream in(argv[1],std::ios::binary);if(!in){std::cerr<<"cannot open classfile\n";return 2;}
 std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
 auto cls=opti::runtime::parse_class_code(bytes.data(),bytes.size());
 if(!cls.valid){std::cerr<<"class parse failed: "<<cls.error<<"\n";return 3;}
 std::vector<std::int32_t> args;
 for(int i=4;i<argc;i++){errno=0;char*end=nullptr;long value=std::strtol(argv[i],&end,10);if(errno||!end||*end||value<std::numeric_limits<std::int32_t>::min()||value>std::numeric_limits<std::int32_t>::max()){std::cerr<<"invalid int argument: "<<argv[i]<<"\n";return 2;}args.push_back(static_cast<std::int32_t>(value));}
 opti::runtime::IntegerBytecodeInterpreter vm;
 auto result=vm.invoke_static_int(cls,argv[2],argv[3],args);
 if(!result.success){std::cerr<<"execution failed: "<<result.error<<"\n";return 4;}
 std::cout<<result.int_value<<"\ninstructions="<<result.instructions_executed<<"\n";
 return 0;
}
