#include "class_code_parser.hpp"
#include <limits>
namespace opti::runtime {
namespace {
struct Reader {
 const std::uint8_t* p; std::size_t n,pos=0;
 bool take(std::size_t k){if(pos>n||k>n-pos)return false;pos+=k;return true;}
 bool u1(std::uint8_t&v){if(!take(1))return false;v=p[pos-1];return true;}
 bool u2(std::uint16_t&v){if(!take(2))return false;v=(std::uint16_t(p[pos-2])<<8)|p[pos-1];return true;}
 bool u4(std::uint32_t&v){if(!take(4))return false;v=(std::uint32_t(p[pos-4])<<24)|(std::uint32_t(p[pos-3])<<16)|(std::uint32_t(p[pos-2])<<8)|p[pos-1];return true;}
};
bool cp_tag(const std::vector<ConstantPoolEntry>&cp,std::uint16_t i,std::uint8_t tag){return i>0&&i<cp.size()&&cp[i].tag==tag;}
bool skip_attributes(Reader&r,const std::vector<ConstantPoolEntry>&cp,std::uint16_t count){
 for(std::uint32_t i=0;i<count;i++){std::uint16_t name;std::uint32_t len;if(!r.u2(name)||!r.u4(len)||!cp_tag(cp,name,1)||!r.take(len))return false;}return true;
}
bool parse_code_attribute(const std::uint8_t* p,std::size_t n,const std::vector<ConstantPoolEntry>&cp,MethodCode&out){
 Reader r{p,n};std::uint32_t code_len;std::uint16_t ex_count,attr_count;
 if(!r.u2(out.max_stack)||!r.u2(out.max_locals)||!r.u4(code_len)||code_len>65535||!r.take(code_len))return false;
 out.code.assign(p+r.pos-code_len,p+r.pos);
 if(!r.u2(ex_count))return false;
 for(std::uint32_t i=0;i<ex_count;i++){ExceptionHandler e;if(!r.u2(e.start_pc)||!r.u2(e.end_pc)||!r.u2(e.handler_pc)||!r.u2(e.catch_type))return false;
   if(e.start_pc>=e.end_pc||e.end_pc>code_len||e.handler_pc>=code_len)return false;
   if(e.catch_type&&!cp_tag(cp,e.catch_type,7))return false;
   out.exception_table.push_back(e);
 }
 if(!r.u2(attr_count)||!skip_attributes(r,cp,attr_count)||r.pos!=r.n)return false;
 return true;
}
}
ParsedClassCode parse_class_code(const std::uint8_t* bytes,std::size_t size){
 ParsedClassCode out;auto fail=[&](const char*m){out.error=m;return out;};
 if(!bytes||size<10)return fail("classfile header truncated");
 Reader r{bytes,size};std::uint32_t magic;std::uint16_t cp_count;
 if(!r.u4(magic)||magic!=0xcafebabe)return fail("invalid classfile magic");
 if(!r.u2(out.minor)||!r.u2(out.major)||!r.u2(cp_count)||cp_count==0)return fail("invalid classfile version/constant pool header");
 if(out.major<45||out.major>65)return fail("unsupported classfile major version");
 out.constant_pool.resize(cp_count);
 for(std::uint32_t i=1;i<cp_count;i++){
  std::uint8_t tag;if(!r.u1(tag))return fail("truncated constant pool tag");auto&c=out.constant_pool[i];c.tag=tag;
  std::uint16_t v;
  switch(tag){
   case 1: if(!r.u2(v)||!r.take(v))return fail("truncated UTF8 constant");c.utf8.assign(reinterpret_cast<const char*>(bytes+r.pos-v),v);break;
   case 3: case 4:{std::uint32_t x;if(!r.u4(x))return fail("truncated numeric constant");c.integer=static_cast<std::int32_t>(x);break;}
   case 5: case 6:{std::uint32_t hi,lo;if(!r.u4(hi)||!r.u4(lo)||i+1>=cp_count)return fail("truncated wide constant");c.long_integer=static_cast<std::int64_t>((std::uint64_t(hi)<<32)|lo);out.constant_pool[++i].tag=0;break;}
   case 7: case 8: case 16: case 19: case 20: if(!r.u2(c.a))return fail("truncated constant pool index");break;
   case 9: case 10: case 11: case 12: case 17: case 18: if(!r.u2(c.a)||!r.u2(c.b))return fail("truncated paired constant");break;
   case 15:{std::uint8_t kind;if(!r.u1(kind)||!r.u2(c.a))return fail("truncated method handle");c.b=kind;break;}
   default:return fail("unknown constant pool tag");
  }
 }
 // Validate all UTF8-indexed entries used by this parser.
 for(std::size_t i=1;i<out.constant_pool.size();i++){const auto&c=out.constant_pool[i];
  if(c.tag==7||c.tag==8||c.tag==16||c.tag==19||c.tag==20){if(!cp_tag(out.constant_pool,c.a,1))return fail("invalid constant pool UTF8 reference");}
  if(c.tag==9||c.tag==10||c.tag==11){if(!cp_tag(out.constant_pool,c.a,7)||!cp_tag(out.constant_pool,c.b,12))return fail("invalid member reference");}
  if(c.tag==12){if(!cp_tag(out.constant_pool,c.a,1)||!cp_tag(out.constant_pool,c.b,1))return fail("invalid name-and-type reference");}
 }
 std::uint16_t this_idx,super_idx,interfaces,fields,methods,class_attrs;
 if(!r.u2(out.access_flags)||!r.u2(this_idx)||!r.u2(super_idx)||!cp_tag(out.constant_pool,this_idx,7))return fail("invalid class declaration");
 out.this_class=out.constant_pool[out.constant_pool[this_idx].a].utf8;
 if(super_idx){if(!cp_tag(out.constant_pool,super_idx,7))return fail("invalid superclass reference");out.super_class=out.constant_pool[out.constant_pool[super_idx].a].utf8;}
 if(!r.u2(interfaces))return fail("truncated interface table");
 for(std::uint32_t i=0;i<interfaces;i++){std::uint16_t idx;if(!r.u2(idx)||!cp_tag(out.constant_pool,idx,7)||!cp_tag(out.constant_pool,out.constant_pool[idx].a,1))return fail("invalid interface class reference");out.interfaces.push_back(out.constant_pool[out.constant_pool[idx].a].utf8);}
 if(!r.u2(fields))return fail("truncated field table");
 for(std::uint32_t i=0;i<fields;i++){
  std::uint16_t a,n,d,ac;if(!r.u2(a)||!r.u2(n)||!r.u2(d)||!r.u2(ac)||!cp_tag(out.constant_pool,n,1)||!cp_tag(out.constant_pool,d,1))return fail("invalid field table");
  FieldInfo field;field.access_flags=a;field.name=out.constant_pool[n].utf8;field.descriptor=out.constant_pool[d].utf8;
  for(std::uint32_t j=0;j<ac;j++){
   std::uint16_t an;std::uint32_t len;if(!r.u2(an)||!r.u4(len)||!cp_tag(out.constant_pool,an,1)||len>r.n-r.pos)return fail("truncated field attribute");
   const auto& attr=out.constant_pool[an].utf8;
   if(attr=="ConstantValue"){
    if(len!=2||field.constant_value_index)return fail("malformed or duplicate ConstantValue attribute");
    std::uint16_t index;if(!r.u2(index)||index==0||index>=out.constant_pool.size())return fail("invalid ConstantValue index");
    field.constant_value_index=index;
   } else if(!r.take(len))return fail("truncated field attribute data");
  }
  if(field.constant_value_index){const auto tag=out.constant_pool[field.constant_value_index].tag;const auto& desc=field.descriptor;
   const bool valid=(desc=="Z"||desc=="B"||desc=="C"||desc=="S"||desc=="I")?tag==3:
    desc=="F"?tag==4:desc=="J"?tag==5:desc=="D"?tag==6:desc=="Ljava/lang/String;"?tag==8:false;
   if(!valid)return fail("ConstantValue constant-pool type does not match field descriptor");
  }
  out.fields.push_back(std::move(field));
 }
 if(!r.u2(methods))return fail("truncated methods count");
 for(std::uint32_t i=0;i<methods;i++){
  MethodCode m;std::uint16_t name,desc,ac;if(!r.u2(m.access_flags)||!r.u2(name)||!r.u2(desc)||!r.u2(ac)||!cp_tag(out.constant_pool,name,1)||!cp_tag(out.constant_pool,desc,1))return fail("invalid method declaration");
  m.name=out.constant_pool[name].utf8;m.descriptor=out.constant_pool[desc].utf8;
  bool code_seen=false;
  for(std::uint32_t j=0;j<ac;j++){std::uint16_t an;std::uint32_t len;if(!r.u2(an)||!r.u4(len)||!cp_tag(out.constant_pool,an,1)||len>r.n-r.pos)return fail("truncated method attribute");
   const auto& attr_name=out.constant_pool[an].utf8;
   if(attr_name=="Code"){if(code_seen)return fail("duplicate Code attribute");code_seen=true;MethodCode parsed=m;if(!parse_code_attribute(r.p+r.pos,len,out.constant_pool,parsed))return fail("malformed Code attribute");m.max_stack=parsed.max_stack;m.max_locals=parsed.max_locals;m.code=std::move(parsed.code);m.exception_table=std::move(parsed.exception_table);}
   if(!r.take(len))return fail("truncated method attribute data");
  }
  if(code_seen)out.methods.push_back(std::move(m));
 }
 if(!r.u2(class_attrs)||!skip_attributes(r,out.constant_pool,class_attrs)||r.pos!=r.n)return fail("malformed class attributes or trailing bytes");
 out.valid=true;return out;
}
}
