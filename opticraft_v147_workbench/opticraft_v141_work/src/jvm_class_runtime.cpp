#include "jvm_class_runtime.hpp"
#include <unordered_set>
namespace opti::runtime {
ClassRuntimeResult JvmClassRuntime::register_class(ParsedClassCode cls){
 if(!cls.valid||cls.this_class.empty())return {false,"class must be parsed and have a name"};
 if(classes_.count(cls.this_class))return {false,"duplicate class definition: "+cls.this_class};
 const std::string key=cls.this_class;
 classes_.emplace(key,std::make_shared<ParsedClassCode>(std::move(cls)));
 return {true,{}};
}
bool JvmClassRuntime::register_resource(const std::string& name,std::vector<std::uint8_t> bytes){if(name.empty()||resources_.count(name))return false;resources_.emplace(name,std::move(bytes));return true;}
bool JvmClassRuntime::find_resource(const std::string& name,std::vector<std::uint8_t>& bytes)const{const auto it=resources_.find(name);if(it==resources_.end())return false;bytes=it->second;return true;}
const ParsedClassCode* JvmClassRuntime::find_class(const std::string&name)const{
 auto it=classes_.find(name);return it==classes_.end()?nullptr:it->second.get();
}
const MethodCode* JvmClassRuntime::declared_method(const ParsedClassCode&c,const std::string&n,const std::string&d)const{
 for(const auto&m:c.methods)if(m.name==n&&m.descriptor==d)return &m;
 return nullptr;
}
ResolvedMethod JvmClassRuntime::resolve_method(const std::string&owner,const std::string&name,const std::string&desc,InvokeKind kind,const std::string&receiver)const{
 const std::string start=(kind==InvokeKind::Virtual&&!receiver.empty())?receiver:owner;
 std::unordered_set<std::string> seen;std::string current=start;
 while(!current.empty()&&seen.insert(current).second){
  const auto*c=find_class(current);if(!c)return {};
  if(const auto*m=declared_method(*c,name,desc)){
   const bool is_static=(m->access_flags&0x0008)!=0;
   if((kind==InvokeKind::Static)!=is_static)return {};
   if(kind==InvokeKind::Virtual&&(m->access_flags&0x0002))return {};
   return {c,m,current};
  }
  if(kind==InvokeKind::Special)break;
  current=c->super_class;
 }
 return {};
}
bool JvmClassRuntime::is_assignable(const std::string&actual,const std::string&expected)const{
 if(actual==expected)return true;
 // Minimal bootstrap hierarchy for exceptions and core marker types. These
 // names remain meaningful even when the host JDK classes are not loaded.
 auto builtin_parent=[](const std::string& n)->std::string {
  if(n=="java/lang/NullPointerException"||n=="java/lang/ArithmeticException"||
     n=="java/lang/ArrayIndexOutOfBoundsException"||n=="java/lang/ArrayStoreException"||
     n=="java/lang/ClassCastException"||n=="java/lang/NegativeArraySizeException"||
     n=="java/lang/IllegalArgumentException"||n=="java/lang/IllegalStateException") return "java/lang/RuntimeException";
  if(n=="java/lang/RuntimeException") return "java/lang/Exception";
  if(n=="java/lang/Exception"||n=="java/lang/Error") return "java/lang/Throwable";
  if(n=="java/lang/Throwable") return "java/lang/Object";
  return {};
 };
 std::string builtin=actual;
 for(unsigned depth=0;!builtin.empty()&&depth<8;++depth){if(builtin==expected)return true;builtin=builtin_parent(builtin);}
 std::unordered_set<std::string>seen;
 std::vector<std::string>pending{actual};
 while(!pending.empty()){
  const auto current=pending.back();pending.pop_back();
  if(current==expected)return true;
  if(!seen.insert(current).second)continue;
  const auto*c=find_class(current);if(!c)continue;
  if(!c->super_class.empty())pending.push_back(c->super_class);
  for(const auto&interface_name:c->interfaces)pending.push_back(interface_name);
 }
 return false;
}
bool JvmClassRuntime::instance_of(const JvmHeap&heap,ObjectRef ref,const std::string&expected)const{
 const auto*object=heap.get(ref);return object&&object->kind==HeapKind::Object&&is_assignable(object->class_name,expected);
}
bool JvmClassRuntime::build_instance_layout(const std::string&name,std::vector<FieldLayout>&out,std::string&error)const{
 std::vector<const ParsedClassCode*> chain;std::unordered_set<std::string>seen;std::string current=name;
 while(!current.empty()){
  if(!seen.insert(current).second){error="cyclic superclass chain at "+current;return false;}
  const auto*c=find_class(current);
  if(!c){
   // Platform classes (java.*, javax.*) are supplied by the VM/host adapter,
   // not by the game JAR. They form an external root for the in-JAR layout.
   if(current.rfind("java/",0)==0||current.rfind("javax/",0)==0)break;
   error="missing superclass: "+current;return false;
  }
  chain.push_back(c);current=c->super_class;
 }
 out.clear();
 for(auto it=chain.rbegin();it!=chain.rend();++it)for(const auto&f:(*it)->fields){
  if(f.access_flags&0x0008)continue;
  if(f.name.empty()||f.descriptor.empty()){error="invalid field declaration";return false;}
  out.push_back({(*it)->this_class+"."+f.name,f.descriptor,false});
 }
 return true;
}

bool JvmClassRuntime::begin_class_initialization(const std::string& name, std::uint64_t thread_id, std::string& error) {
 auto it=initialization_.find(name);
 if(it==initialization_.end()) { if(!find_class(name)){error="class not registered: "+name;return false;} it=initialization_.emplace(name,ClassInitializationRecord{}).first; }
 auto& r=it->second;
 if(r.state==ClassInitializationState::Initialized){error.clear();return true;}
 if(r.state==ClassInitializationState::Erroneous){error="NoClassDefFoundError: "+name+ (r.failure.empty()?std::string{}:" ("+r.failure+")");return false;}
 if(r.state==ClassInitializationState::Initializing){ if(r.owner_thread==thread_id){error.clear();return true;} error="class initialization busy: "+name;return false; }
 r.state=ClassInitializationState::Initializing; r.owner_thread=thread_id; r.failure.clear(); error.clear(); return true;
}
bool JvmClassRuntime::complete_class_initialization(const std::string& name, std::uint64_t thread_id) { auto it=initialization_.find(name); if(it==initialization_.end()||it->second.state!=ClassInitializationState::Initializing||it->second.owner_thread!=thread_id)return false; it->second.state=ClassInitializationState::Initialized;it->second.owner_thread=0;return true; }
bool JvmClassRuntime::fail_class_initialization(const std::string& name, std::uint64_t thread_id, const std::string& cause) { auto it=initialization_.find(name); if(it==initialization_.end()||it->second.state!=ClassInitializationState::Initializing||it->second.owner_thread!=thread_id)return false;it->second.state=ClassInitializationState::Erroneous;it->second.owner_thread=0;it->second.failure=cause;return true; }
ClassInitializationRecord JvmClassRuntime::class_initialization(const std::string& name) const { auto it=initialization_.find(name); return it==initialization_.end()?ClassInitializationRecord{}:it->second; }
}
