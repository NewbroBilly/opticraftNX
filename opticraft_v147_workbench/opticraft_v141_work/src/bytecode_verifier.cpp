#include "bytecode_verifier.hpp"
#include <algorithm>
#include <set>
namespace opti::runtime {
namespace {
bool need(std::size_t p,std::size_t n,std::size_t size){return p<=size && n<=size-p;}
std::uint16_t u2(const std::uint8_t*p){return (std::uint16_t(p[0])<<8)|p[1];}
std::int16_t s2(const std::uint8_t*p){return static_cast<std::int16_t>(u2(p));}
std::int32_t s4(const std::uint8_t*p){return (std::int32_t(p[0])<<24)|(std::int32_t(p[1])<<16)|(std::int32_t(p[2])<<8)|p[3];}
}
BytecodeVerification verify_bytecode_structure(const std::uint8_t* code,std::size_t size){
 BytecodeVerification out; auto fail=[&](const char*m){out.error=m;return out;};
 if(!code && size) return fail("null bytecode buffer");
 if(size>65535) return fail("Code array exceeds JVM 65535-byte limit");
 std::set<std::uint32_t> boundaries; std::vector<std::pair<std::uint32_t,std::int32_t>> branches;
 std::size_t pc=0;
 while(pc<size){
  const std::size_t start=pc; boundaries.insert(static_cast<std::uint32_t>(pc));
  const std::uint8_t op=code[pc++]; std::size_t len=1;
  if(op==0xaa || op==0xab){
   while(pc%4) { if(!need(pc,1,size)) return fail("truncated switch padding"); if(code[pc++]!=0) return fail("nonzero switch padding"); }
   if(!need(pc,op==0xaa?12:8,size)) return fail("truncated switch header");
   const std::int32_t def=s4(code+pc); pc+=4; branches.push_back({static_cast<std::uint32_t>(start),def});
   if(op==0xaa){
    const std::int32_t low=s4(code+pc), high=s4(code+pc+4); pc+=8;
    if(high<low || std::uint64_t(std::int64_t(high)-low+1)>16380) return fail("invalid tableswitch range");
    const std::size_t count=std::size_t(std::int64_t(high)-low+1);
    if(!need(pc,count*4,size)) return fail("truncated tableswitch offsets");
    for(std::size_t i=0;i<count;i++){branches.push_back({static_cast<std::uint32_t>(start),s4(code+pc)});pc+=4;}
   } else {
    const std::int32_t pairs=s4(code+pc); pc+=4;
    if(pairs<0 || pairs>8191 || !need(pc,std::size_t(pairs)*8,size)) return fail("invalid lookupswitch pairs");
    std::int32_t prev=INT32_MIN;
    for(int i=0;i<pairs;i++){std::int32_t key=s4(code+pc),off=s4(code+pc+4);pc+=8;if(i && key<=prev)return fail("lookupswitch keys not strictly increasing");prev=key;branches.push_back({static_cast<std::uint32_t>(start),off});}
   }
   len=pc-start;
  } else if(op==0xc4){
   if(!need(pc,1,size)) return fail("truncated wide opcode");
   const std::uint8_t sub=code[pc++];
   if(sub==0x84){if(!need(pc,4,size))return fail("truncated wide iinc");pc+=4;}
   else if(sub==0x15||sub==0x16||sub==0x17||sub==0x18||sub==0x19||sub==0x36||sub==0x37||sub==0x38||sub==0x39||sub==0x3a||sub==0xa9){if(!need(pc,2,size))return fail("truncated wide local");pc+=2;}
   else return fail("invalid wide sub-opcode");
   len=pc-start;
  } else {
   if(op==0x10||op==0x12||op==0x15||op==0x16||op==0x17||op==0x18||op==0x19||op==0x36||op==0x37||op==0x38||op==0x39||op==0x3a||op==0xa9||op==0xbc)len=2;
   else if(op==0x11||op==0x13||op==0x14||op==0x84|| (op>=0x99&&op<=0xa8)||op==0xc6||op==0xc7 || (op>=0xb2&&op<=0xb8)||op==0xbb||op==0xbd||op==0xc0||op==0xc1)len=3;
   else if(op==0xc5)len=4;
   else if(op==0xb9||op==0xba||op==0xc8||op==0xc9)len=5;
   else if(op>=0xca) return fail("reserved or unsupported opcode");
   if(!need(start,len,size))return fail("truncated instruction");
   if(op==0xb9 && (code[start+3]==0 || code[start+4]!=0)) return fail("invalid invokeinterface operands");
   if(op==0xba && (code[start+3]!=0 || code[start+4]!=0)) return fail("invalid invokedynamic reserved operands");
   if(op==0xbc && (code[start+1]<4 || code[start+1]>11)) return fail("invalid newarray atype");
   if(op==0xc5 && code[start+3]==0) return fail("multianewarray dimensions must be nonzero");
   if((op>=0x99&&op<=0xa8) || op==0xc6||op==0xc7) branches.push_back({static_cast<std::uint32_t>(start),s2(code+start+1)});
   if(op==0xc8||op==0xc9) branches.push_back({static_cast<std::uint32_t>(start),s4(code+start+1)});
   pc=start+len;
  }
  out.instructions.push_back({static_cast<std::uint32_t>(start),op,static_cast<std::uint32_t>(len)});
 }
 boundaries.insert(static_cast<std::uint32_t>(size));
 for(const auto&b:branches){std::int64_t target=std::int64_t(b.first)+b.second;if(target<0||target>=static_cast<std::int64_t>(size)||!boundaries.count(static_cast<std::uint32_t>(target)))return fail("branch target is not an instruction boundary");out.branch_targets.push_back(static_cast<std::uint32_t>(target));}
 out.valid=true; return out;
}
}
