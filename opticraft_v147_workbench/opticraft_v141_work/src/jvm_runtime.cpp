#include "jvm_runtime.hpp"
#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_set>
namespace opti::runtime {
namespace {
bool is_reference(const JvmValue&v,ObjectRef&out){if(const auto*p=std::get_if<ObjectRef>(&v)){out=*p;return true;}return false;}
}
ObjectRef JvmHeap::allocate_object(const std::string&name){
 if(name.empty()||objects_.size()>=max_objects_||next_ref_==0)return 0;
 const ObjectRef id=next_ref_++;HeapObject obj;obj.kind=HeapKind::Object;obj.class_name=name;
 objects_.emplace(id,std::move(obj));++allocated_;return id;
}
ObjectRef JvmHeap::allocate_string(const std::string& value){
 ObjectRef id=allocate_object("java/lang/String");if(!id)return 0;auto*obj=get(id);obj->string_value=value;return id;
}
bool JvmHeap::get_string(ObjectRef ref,std::string& value)const{const auto*obj=get(ref);if(!obj||obj->class_name!="java/lang/String")return false;value=obj->string_value;return true;
}
ObjectRef JvmHeap::allocate_instance(const std::string&name,const std::vector<FieldLayout>&fields){
 ObjectRef id=allocate_object(name);if(!id)return 0;auto*obj=get(id);
 for(const auto&field:fields){if(field.is_static)continue;if(field.name.empty()||field.descriptor.empty()){objects_.erase(id);return 0;}
  JvmValue initial=NullValue{};
  const auto&d=field.descriptor;
  if(d=="Z"||d=="B"||d=="C"||d=="S"||d=="I")initial=std::int32_t(0);
  else if(d=="F")initial=0.0f;
  else if(d=="J")initial=std::int64_t(0);
  else if(d=="D")initial=0.0;
  else if(d.front()!='L'&&d.front()!='['){objects_.erase(id);return 0;}
  if(!obj->fields.emplace(field.name,initial).second){objects_.erase(id);return 0;}
 }
 return id;
}
ObjectRef JvmHeap::allocate_int_array(std::size_t n){ return allocate_primitive_array(n, 'I'); }
ObjectRef JvmHeap::allocate_primitive_array(std::size_t n, char descriptor){
 if(objects_.size()>=max_objects_||next_ref_==0||n>std::numeric_limits<std::uint32_t>::max())return 0;
 if(std::string("ZBCSIJFD").find(descriptor)==std::string::npos)return 0;
 const ObjectRef id=next_ref_++;HeapObject obj;obj.kind=descriptor=='I'?HeapKind::IntArray:HeapKind::PrimitiveArray;
 obj.class_name=std::string("[")+descriptor;obj.component_descriptor=std::string(1,descriptor);
 JvmValue initial=std::int32_t(0);
 if(descriptor=='J')initial=std::int64_t(0);else if(descriptor=='F')initial=0.0f;else if(descriptor=='D')initial=0.0;
 obj.elements.assign(n,initial);objects_.emplace(id,std::move(obj));++allocated_;return id;
}
ObjectRef JvmHeap::allocate_byte_stream(const std::vector<std::uint8_t>& bytes){
 ObjectRef id=allocate_object("java/io/ByteArrayInputStream");if(!id)return 0;auto*obj=get(id);obj->stream_bytes=bytes;return id;
}
ObjectRef JvmHeap::allocate_reference_array(std::size_t n,const std::string&component){
 if(component.empty()||objects_.size()>=max_objects_||next_ref_==0||n>std::numeric_limits<std::uint32_t>::max())return 0;
 const ObjectRef id=next_ref_++;HeapObject obj;obj.kind=HeapKind::ReferenceArray;
 obj.class_name=!component.empty()&&component.front()=='['?"["+component:"[L"+component+";";
 obj.component_descriptor=component;obj.elements.assign(n,NullValue{});
 objects_.emplace(id,std::move(obj));++allocated_;return id;
}
bool JvmHeap::valid_ref(ObjectRef ref)const{return ref!=0&&objects_.find(ref)!=objects_.end();}
const HeapObject* JvmHeap::get(ObjectRef ref)const{auto it=objects_.find(ref);return it==objects_.end()?nullptr:&it->second;}
HeapObject* JvmHeap::get(ObjectRef ref){auto it=objects_.find(ref);return it==objects_.end()?nullptr:&it->second;}
bool JvmHeap::put_field(ObjectRef ref,const std::string&key,const JvmValue&v){auto*o=get(ref);if(!o||o->kind!=HeapKind::Object||key.empty())return false;ObjectRef child=0;if(is_reference(v,child)&&child&&!valid_ref(child))return false;o->fields[key]=v;return true;}
bool JvmHeap::get_field(ObjectRef ref,const std::string&key,JvmValue&v)const{auto*o=get(ref);if(!o||o->kind!=HeapKind::Object)return false;auto it=o->fields.find(key);if(it==o->fields.end())return false;v=it->second;return true;}
bool JvmHeap::array_store(ObjectRef ref,std::size_t i,const JvmValue&v){
 auto*o=get(ref);if(!o||i>=o->elements.size())return false;
 if(o->kind==HeapKind::IntArray || o->kind==HeapKind::PrimitiveArray){
  const char d=o->component_descriptor.empty()?'I':o->component_descriptor[0];
  const bool ok=(std::string("ZBCSI").find(d)!=std::string::npos&&std::holds_alternative<std::int32_t>(v))||
   (d=='J'&&std::holds_alternative<std::int64_t>(v))||(d=='F'&&std::holds_alternative<float>(v))||
   (d=='D'&&std::holds_alternative<double>(v));
  if(!ok)return false;
 } else if(o->kind==HeapKind::ReferenceArray){
  if(!std::holds_alternative<NullValue>(v)&&!std::holds_alternative<ObjectRef>(v))return false;
  ObjectRef child=0;if(is_reference(v,child)&&child&&!valid_ref(child))return false;
 } else return false;
 o->elements[i]=v;return true;
}
bool JvmHeap::array_load(ObjectRef ref,std::size_t i,JvmValue&v)const{auto*o=get(ref);if(!o||o->kind==HeapKind::Object||i>=o->elements.size())return false;v=o->elements[i];return true;}
std::size_t JvmHeap::collect_garbage(const std::vector<JvmValue>&roots){
 std::unordered_set<ObjectRef> marked;std::queue<ObjectRef> pending;
 auto mark=[&](ObjectRef ref){if(valid_ref(ref)&&marked.insert(ref).second)pending.push(ref);};
 for(const auto&v:roots){ObjectRef ref=0;if(is_reference(v,ref))mark(ref);}
 while(!pending.empty()){const auto id=pending.front();pending.pop();const auto*o=get(id);if(!o)continue;
  for(const auto&entry:o->fields){ObjectRef child=0;if(is_reference(entry.second,child))mark(child);}
  for(const auto&v:o->elements){ObjectRef child=0;if(is_reference(v,child))mark(child);}
 }
 std::size_t removed=0;
 for(auto it=objects_.begin();it!=objects_.end();){if(!marked.count(it->first)){it=objects_.erase(it);++removed;}else ++it;}
 collected_+=removed;return removed;
}
HeapStats JvmHeap::stats()const{
 HeapStats s;s.live_objects=objects_.size();s.allocated_objects=allocated_;s.collected_objects=collected_;
 for(const auto&pair:objects_){const auto&o=pair.second;s.live_bytes_estimate+=sizeof(HeapObject)+o.class_name.capacity()+o.component_descriptor.capacity()+o.elements.capacity()*sizeof(JvmValue);for(const auto&f:o.fields)s.live_bytes_estimate+=f.first.capacity()+sizeof(JvmValue);}
 return s;
}
std::string JvmStaticFields::key(const std::string&c,const std::string&f){return c+"#"+f;}
bool JvmStaticFields::define(const std::string&c,const std::string&f,const JvmValue&v){if(c.empty()||f.empty())return false;return values_.emplace(key(c,f),v).second;}
bool JvmStaticFields::set(const std::string&c,const std::string&f,const JvmValue&v){auto it=values_.find(key(c,f));if(it==values_.end())return false;it->second=v;return true;}
bool JvmStaticFields::get(const std::string&c,const std::string&f,JvmValue&v)const{auto it=values_.find(key(c,f));if(it==values_.end())return false;v=it->second;return true;}
std::vector<JvmValue> JvmStaticFields::roots()const{std::vector<JvmValue>r;for(const auto&v:values_)r.push_back(v.second);return r;}
}
