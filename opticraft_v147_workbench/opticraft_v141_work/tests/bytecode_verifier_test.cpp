#include "bytecode_verifier.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace opti::runtime;
int main(){
 const std::uint8_t simple[]={0x03,0x3b,0xa7,0x00,0x03,0xb1};
 auto a=verify_bytecode_structure(simple,sizeof(simple)); assert(a.valid); assert(a.instructions.size()==4);
 const std::uint8_t bad_branch[]={0xa7,0x00,0x02,0xb1};
 assert(!verify_bytecode_structure(bad_branch,sizeof(bad_branch)).valid);
 const std::uint8_t bad_wide[]={0xc4,0x00,0x00};
 assert(!verify_bytecode_structure(bad_wide,sizeof(bad_wide)).valid);
 const std::uint8_t bad_invoke[]={0xb9,0x00,0x01,0x00,0x01};
 assert(!verify_bytecode_structure(bad_invoke,sizeof(bad_invoke)).valid);
 const std::uint8_t truncated[]={0x11,0x00};
 assert(!verify_bytecode_structure(truncated,sizeof(truncated)).valid);
 const std::uint8_t lookup[]={0xab,0,0,0,0,0,0,20,0,0,0,1,0,0,0,4,0,0,0,20,0xb1};
 assert(verify_bytecode_structure(lookup,sizeof(lookup)).valid);
 std::cout<<"bytecode structural verifier tests passed\n";
}
