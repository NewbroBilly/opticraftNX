#include "jvm_exception_runtime.hpp"
namespace opti::runtime {
ExceptionHandlerMatch find_exception_handler(const ParsedClassCode&cls,const MethodCode&m,std::uint16_t pc,const std::string&thrown,const JvmClassRuntime&classes){
 for(const auto&h:m.exception_table){
  if(pc<h.start_pc||pc>=h.end_pc)continue;
  if(h.catch_type==0)return {true,h.handler_pc,0};
  if(h.catch_type>=cls.constant_pool.size())continue;
  const auto&entry=cls.constant_pool[h.catch_type];
  if(entry.tag!=7||entry.a==0||entry.a>=cls.constant_pool.size())continue;
  const auto&name=cls.constant_pool[entry.a].utf8;
  if(classes.is_assignable(thrown,name))return {true,h.handler_pc,h.catch_type};
 }
 return {};
}
}
