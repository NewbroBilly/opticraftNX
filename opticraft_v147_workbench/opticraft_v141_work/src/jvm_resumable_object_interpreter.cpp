#include "jvm_resumable_object_interpreter.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace opti::runtime {
namespace {
std::uint16_t read_u2(const std::vector<std::uint8_t>& b, std::size_t p) {
    return static_cast<std::uint16_t>((std::uint16_t(b[p]) << 8) | b[p + 1]);
}
std::int16_t read_s2(const std::vector<std::uint8_t>& b, std::size_t p) {
    return static_cast<std::int16_t>(read_u2(b, p));
}
std::string cp_class_name(const ParsedClassCode& c, std::uint16_t index) {
    if (!index || index >= c.constant_pool.size()) return {};
    const auto& e = c.constant_pool[index];
    if (e.tag != 7 || !e.a || e.a >= c.constant_pool.size()) return {};
    return c.constant_pool[e.a].utf8;
}
bool cp_member(const ParsedClassCode& c, std::uint16_t index,
               std::string& owner, std::string& name, std::string& desc,
               std::uint8_t expected_a, std::uint8_t expected_b) {
    if (!index || index >= c.constant_pool.size()) return false;
    const auto& r = c.constant_pool[index];
    if (r.tag != expected_a && r.tag != expected_b) return false;
    if (!r.a || !r.b || r.a >= c.constant_pool.size() || r.b >= c.constant_pool.size()) return false;
    const auto& cl = c.constant_pool[r.a];
    const auto& nt = c.constant_pool[r.b];
    if (cl.tag != 7 || nt.tag != 12 || !cl.a || !nt.a || !nt.b ||
        cl.a >= c.constant_pool.size() || nt.a >= c.constant_pool.size() || nt.b >= c.constant_pool.size()) return false;
    owner = c.constant_pool[cl.a].utf8;
    name = c.constant_pool[nt.a].utf8;
    desc = c.constant_pool[nt.b].utf8;
    return !owner.empty() && !name.empty() && !desc.empty();
}
bool parse_args(const std::string& d, std::vector<char>& types, char& result) {
    if (d.empty() || d[0] != '(') return false;
    std::size_t p = 1;
    while (p < d.size() && d[p] != ')') {
        char t = d[p++];
        if (t == 'L') { auto end = d.find(';', p); if (end == std::string::npos) return false; p = end + 1; types.push_back('A'); }
        else if (t == '[') { while (p < d.size() && d[p] == '[') ++p; if (p >= d.size()) return false; if (d[p] == 'L') { auto end=d.find(';',p); if(end==std::string::npos)return false;p=end+1; } else ++p; types.push_back('A'); }
        else if (t == 'I' || t == 'Z' || t == 'B' || t == 'C' || t == 'S') types.push_back('I');
        else if (t == 'F') types.push_back('F');
        else if (t == 'J' || t == 'D') return false; // category-2 slots are not enabled in this migration layer
        else return false;
    }
    if (p >= d.size() || d[p++] != ')' || p >= d.size()) return false;
    result = d[p];
    return true;
}
bool compatible_value(char t, const JvmValue& v) {
    if (t == 'A') return std::holds_alternative<NullValue>(v) || std::holds_alternative<ObjectRef>(v);
    if (t == 'I') return std::holds_alternative<std::int32_t>(v);
    if (t == 'F') return std::holds_alternative<float>(v);
    return false;
}
}

JvmResumableObjectInterpreter::JvmResumableObjectInterpreter(
    JvmHeap& heap, JvmStaticFields& statics, JvmClassRuntime& classes, std::size_t depth)
    : heap_(heap), statics_(statics), classes_(classes), max_call_depth_(depth ? depth : 1) {}

bool JvmResumableObjectInterpreter::push_frame(const ResolvedMethod& resolved,
                                                ObjectRef receiver,
                                                const std::vector<JvmValue>& args) {
    if (!resolved.owner || !resolved.method || frames_.size() >= max_call_depth_) return false;
    std::vector<char> types; char ret = 0;
    if (!parse_args(resolved.method->descriptor, types, ret) || types.size() != args.size()) return false;
    const bool is_static = (resolved.method->access_flags & 0x0008) != 0;
    if (is_static == (receiver != 0)) return false;
    if (args.size() + (is_static ? 0u : 1u) > resolved.method->max_locals) return false;
    for (std::size_t i=0;i<args.size();++i) if (!compatible_value(types[i],args[i])) return false;
    Frame f; f.cls=resolved.owner; f.method=resolved.method; f.receiver=receiver;
    f.locals.resize(resolved.method->max_locals, NullValue{});
    f.stack.reserve(resolved.method->max_stack);
    std::size_t slot=0;
    if (!is_static) f.locals[slot++]=receiver;
    for (const auto& arg:args) f.locals[slot++]=arg;
    frames_.push_back(std::move(f));
    return true;
}

bool JvmResumableObjectInterpreter::begin_static(const std::string& owner,
    const std::string& name,const std::string& desc,const std::vector<JvmValue>& args) {
    reset(); auto r=classes_.resolve_method(owner,name,desc,InvokeKind::Static);
    if (!push_frame(r,0,args)) { fail("cannot resolve or initialize static entry method"); return false; }
    result_.status=Status::Ready; return true;
}
bool JvmResumableObjectInterpreter::begin_instance(ObjectRef receiver,
    const std::string& name,const std::string& desc,const std::vector<JvmValue>& args) {
    reset(); const auto* obj=heap_.get(receiver);
    if (!obj || obj->kind!=HeapKind::Object) { fail("invalid instance receiver"); return false; }
    auto r=classes_.resolve_method(obj->class_name,name,desc,InvokeKind::Virtual,obj->class_name);
    if (!push_frame(r,receiver,args)) { fail("cannot resolve or initialize instance entry method"); return false; }
    result_.status=Status::Ready; return true;
}

JvmResumableObjectInterpreter::Result JvmResumableObjectInterpreter::fail(const std::string& msg) {
    result_.status=Status::Failed; result_.error=msg; result_.instructions_executed=total_instructions_; return result_;
}

bool JvmResumableObjectInterpreter::dispatch_exception(Frame& frame, std::size_t throw_pc, ObjectRef exception) {
    if (!heap_.valid_ref(exception)) return false;
    for (const auto& handler : frame.method->exception_table) {
        if (throw_pc < handler.start_pc || throw_pc >= handler.end_pc) continue;
        if (handler.catch_type != 0) {
            const auto catch_name = cp_class_name(*frame.cls, handler.catch_type);
            const auto* thrown = heap_.get(exception);
            if (catch_name.empty() || !thrown || !classes_.is_assignable(thrown->class_name, catch_name)) continue;
        }
        frame.stack.clear();
        if (frame.method->max_stack == 0) return false;
        frame.stack.push_back(exception);
        frame.pc = handler.handler_pc;
        return true;
    }
    return false;
}

bool JvmResumableObjectInterpreter::propagate_exception(std::size_t throw_pc, ObjectRef exception) {
    while (!frames_.empty()) {
        Frame& current = frames_.back();
        if (dispatch_exception(current, throw_pc, exception)) return true;
        frames_.pop_back();
        if (frames_.empty()) break;
        // The caller's invoke instruction is the protected operation that
        // caused the callee to execute. Use its saved bytecode PC when looking
        // for a caller-side handler.
        throw_pc = frames_.back().call_site_pc;
    }
    return false;
}

bool JvmResumableObjectInterpreter::raise_exception(std::size_t throw_pc,
        const std::string& class_name, const std::string& message) {
    const ObjectRef exception = heap_.allocate_object(class_name);
    if (!exception) { fail("cannot allocate Java exception: " + class_name); return false; }
    if (!propagate_exception(throw_pc, exception)) {
        fail("uncaught Java exception: " + class_name + (message.empty() ? "" : ": " + message));
        return false;
    }
    return true;
}

bool JvmResumableObjectInterpreter::initialize_static_storage(const ParsedClassCode& cls) {
    for (const auto& field : cls.fields) {
        if (!(field.access_flags & 0x0008)) continue;
        JvmValue value = NullValue{};
        const auto& d = field.descriptor;
        if (d=="Z"||d=="B"||d=="C"||d=="S"||d=="I") value=std::int32_t(0);
        else if (d=="F") value=0.0f;
        else if (d=="J") value=std::int64_t(0);
        else if (d=="D") value=0.0;
        if (field.constant_value_index && field.constant_value_index < cls.constant_pool.size()) {
            const auto& cp = cls.constant_pool[field.constant_value_index];
            if (cp.tag==3) value=cp.integer;
            else if (cp.tag==4) { std::uint32_t bits=static_cast<std::uint32_t>(cp.integer); float f=0; std::memcpy(&f,&bits,sizeof(f)); value=f; }
            else if (cp.tag==5) value=cp.long_integer;
            else if (cp.tag==6) { std::uint64_t bits=static_cast<std::uint64_t>(cp.long_integer); double dvalue=0; std::memcpy(&dvalue,&bits,sizeof(dvalue)); value=dvalue; }
            else if (cp.tag==8 && cp.a && cp.a<cls.constant_pool.size()) value=heap_.allocate_string(cls.constant_pool[cp.a].utf8);
        }
        if (!statics_.get(cls.this_class,field.name,value)) statics_.define(cls.this_class,field.name,value);
    }
    return true;
}

JvmResumableObjectInterpreter::Result JvmResumableObjectInterpreter::step(std::size_t quantum) {
    if (result_.status==Status::Completed || result_.status==Status::Failed) return result_;
    if (!quantum) { result_.status=Status::Yielded; return result_; }
    result_.status=Status::Yielded; std::size_t executed=0;
    while (executed<quantum && !frames_.empty()) {
        Frame& f=frames_.back(); const auto& code=f.method->code;
        if (f.pc>=code.size()) return fail("method reached end without return");
        const std::size_t start=f.pc; const std::uint8_t op=code[f.pc++];
        auto need=[&](std::size_t n){return f.pc<=code.size() && n<=code.size()-f.pc;};
        auto push=[&](JvmValue v){if(f.stack.size()>=f.method->max_stack)return false;f.stack.push_back(std::move(v));return true;};
        auto pop=[&](JvmValue& v){if(f.stack.empty())return false;v=std::move(f.stack.back());f.stack.pop_back();return true;};
        auto local=[&](std::size_t i,JvmValue& v){if(i>=f.locals.size())return false;v=f.locals[i];return true;};
        auto store=[&](std::size_t i,JvmValue v){if(i>=f.locals.size())return false;f.locals[i]=std::move(v);return true;};
        auto branch=[&](std::int16_t d){const auto t=std::int64_t(start)+d;if(t<0||t>=std::int64_t(code.size()))return false;f.pc=static_cast<std::size_t>(t);return true;};
        bool ok=true; JvmValue a=NullValue{},b=NullValue{};
        if(op==0x00) {}
        else if(op==0x01) ok=push(NullValue{});
        else if(op>=0x02&&op<=0x08) ok=push(std::int32_t(op)-3);
        else if(op==0x10) {if(!need(1))return fail("truncated bipush");ok=push(std::int32_t(static_cast<std::int8_t>(code[f.pc++])));}
        else if(op==0x11) {if(!need(2))return fail("truncated sipush");ok=push(std::int32_t(read_s2(code,f.pc)));f.pc+=2;}
        else if(op==0x15||op==0x19||op==0x36||op==0x3a) {if(!need(1))return fail("truncated local opcode");auto i=code[f.pc++];if(op==0x15||op==0x19){JvmValue v;ok=local(i,v)&&((op==0x15)?std::holds_alternative<std::int32_t>(v):(std::holds_alternative<ObjectRef>(v)||std::holds_alternative<NullValue>(v)));if(ok)ok=push(v);}else ok=pop(a)&&((op==0x36)?std::holds_alternative<std::int32_t>(a):(std::holds_alternative<ObjectRef>(a)||std::holds_alternative<NullValue>(a)))&&store(i,a);}
        else if(op>=0x1a&&op<=0x1d) {JvmValue v;ok=local(op-0x1a,v)&&std::holds_alternative<std::int32_t>(v)&&push(v);}
        else if(op>=0x2a&&op<=0x2d) {JvmValue v;ok=local(op-0x2a,v)&&(std::holds_alternative<ObjectRef>(v)||std::holds_alternative<NullValue>(v))&&push(v);}
        else if(op>=0x3b&&op<=0x3e) ok=pop(a)&&std::holds_alternative<std::int32_t>(a)&&store(op-0x3b,a);
        else if(op>=0x4b&&op<=0x4e) ok=pop(a)&&(std::holds_alternative<ObjectRef>(a)||std::holds_alternative<NullValue>(a))&&store(op-0x4b,a);
        else if(op==0x57) ok=pop(a);
        else if(op==0x59) {if(f.stack.empty())ok=false;else ok=push(f.stack.back());}
        else if(op==0x60||op==0x64||op==0x68||op==0x6c) {if(!pop(b)||!pop(a)||!std::holds_alternative<std::int32_t>(a)||!std::holds_alternative<std::int32_t>(b))return fail("integer arithmetic type/stack error");auto x=std::get<std::int32_t>(a),y=std::get<std::int32_t>(b);if(op==0x6c&&!y){if(!raise_exception(start,"java/lang/ArithmeticException","/ by zero"))return result_;++executed;++total_instructions_;continue;}std::int32_t v=op==0x60?x+y:op==0x64?x-y:op==0x68?x*y:((x==INT32_MIN&&y==-1)?INT32_MIN:x/y);ok=push(v);}
        else if(op==0x84) {if(!need(2))return fail("truncated iinc");auto i=code[f.pc++];auto d=static_cast<std::int8_t>(code[f.pc++]);JvmValue v;if(!local(i,v)||!std::holds_alternative<std::int32_t>(v))return fail("iinc local type error");ok=store(i,std::get<std::int32_t>(v)+d);}
        else if(op>=0x99&&op<=0x9e) {if(!need(2))return fail("truncated conditional branch");auto d=read_s2(code,f.pc);f.pc+=2;if(!pop(a)||!std::holds_alternative<std::int32_t>(a))return fail("branch stack type error");auto x=std::get<std::int32_t>(a);bool take=op==0x99?x==0:op==0x9a?x!=0:op==0x9b?x<0:op==0x9c?x>=0:op==0x9d?x>0:x<=0;if(take&&!branch(d))return fail("invalid branch target");}
        else if(op==0xa7) {if(!need(2))return fail("truncated goto");auto d=read_s2(code,f.pc);f.pc+=2;if(!branch(d))return fail("invalid goto target");}
        else if(op==0x2e || op==0x32) { // iaload / aaload
            if(!pop(b)||!pop(a)||!std::holds_alternative<std::int32_t>(b)||(!std::holds_alternative<ObjectRef>(a)&&!std::holds_alternative<NullValue>(a))) return fail("array load stack/type error");
            if(std::holds_alternative<NullValue>(a)){if(!raise_exception(start,"java/lang/NullPointerException","array load on null"))return result_;++executed;++total_instructions_;continue;}
            const auto index=std::get<std::int32_t>(b); const auto ref=std::get<ObjectRef>(a); JvmValue value;
            const auto* array=heap_.get(ref);
            if(!array) { if(!raise_exception(start,"java/lang/NullPointerException","array load on null"))return result_;++executed;++total_instructions_;continue; }
            if(index<0||static_cast<std::size_t>(index)>=array->elements.size()) { if(!raise_exception(start,"java/lang/ArrayIndexOutOfBoundsException",std::to_string(index)))return result_;++executed;++total_instructions_;continue; }
            if(!heap_.array_load(ref,static_cast<std::size_t>(index),value)) return fail("array load type error");
            if((op==0x2e && !std::holds_alternative<std::int32_t>(value)) || (op==0x32 && !std::holds_alternative<ObjectRef>(value) && !std::holds_alternative<NullValue>(value))) return fail("array load component type error");
            ok=push(value);
        }
        else if(op==0x4f || op==0x53) { // iastore / aastore
            JvmValue value, index_value, array_value;
            if(!pop(value)||!pop(index_value)||!pop(array_value)||!std::holds_alternative<std::int32_t>(index_value)||(!std::holds_alternative<ObjectRef>(array_value)&&!std::holds_alternative<NullValue>(array_value))) return fail("array store stack/type error");
            if(std::holds_alternative<NullValue>(array_value)){if(!raise_exception(start,"java/lang/NullPointerException","array store on null"))return result_;++executed;++total_instructions_;continue;}
            const auto index=std::get<std::int32_t>(index_value); const auto ref=std::get<ObjectRef>(array_value);
            const auto* array=heap_.get(ref);
            if(!array) { if(!raise_exception(start,"java/lang/NullPointerException","array store on null"))return result_;++executed;++total_instructions_;continue; }
            if(index<0||static_cast<std::size_t>(index)>=array->elements.size()) { if(!raise_exception(start,"java/lang/ArrayIndexOutOfBoundsException",std::to_string(index)))return result_;++executed;++total_instructions_;continue; }
            if(op==0x53) {
                if(!std::holds_alternative<ObjectRef>(value)&&!std::holds_alternative<NullValue>(value))return fail("aastore value is not a reference");
                if(array->kind!=HeapKind::ReferenceArray)return fail("aastore target is not a reference array");
                if(const auto* child=std::get_if<ObjectRef>(&value);child&&*child) {
                    const auto* stored=heap_.get(*child);
                    if(!stored||!classes_.is_assignable(stored->class_name,array->component_descriptor)) { if(!raise_exception(start,"java/lang/ArrayStoreException",stored?stored->class_name:"invalid reference"))return result_;++executed;++total_instructions_;continue; }
                }
            }
            if(op==0x4f&&!std::holds_alternative<std::int32_t>(value))return fail("iastore value type error");
            if(!heap_.array_store(ref,static_cast<std::size_t>(index),value))return fail("array store type error");
        }
        else if(op==0xbe) { // arraylength
            if(!pop(a)||(!std::holds_alternative<ObjectRef>(a)&&!std::holds_alternative<NullValue>(a))) return fail("arraylength receiver error");
            if(std::holds_alternative<NullValue>(a)){if(!raise_exception(start,"java/lang/NullPointerException","arraylength on null"))return result_;++executed;++total_instructions_;continue;}
            const auto* array=heap_.get(std::get<ObjectRef>(a));
            if(!array||array->kind==HeapKind::Object||array->elements.size()>static_cast<std::size_t>(INT32_MAX)) return fail("arraylength invalid array");
            ok=push(static_cast<std::int32_t>(array->elements.size()));
        }
        else if(op==0xbc) { // newarray
            if(!need(1)||!pop(a)||!std::holds_alternative<std::int32_t>(a)) return fail("newarray operand error");
            const auto atype=code[f.pc++]; const auto length=std::get<std::int32_t>(a); if(length<0){if(!raise_exception(start,"java/lang/NegativeArraySizeException",std::to_string(length)))return result_;++executed;++total_instructions_;continue;}
            const char descriptor=atype==4?'Z':atype==5?'C':atype==6?'F':atype==7?'D':atype==8?'B':atype==9?'S':atype==10?'I':atype==11?'J':'?';
            if(descriptor=='?') return fail("unsupported newarray type");
            const auto ref=heap_.allocate_primitive_array(static_cast<std::size_t>(length),descriptor); if(!ref)return fail("newarray allocation failed"); ok=push(ref);
        }
        else if(op==0xbd) { // anewarray
            if(!need(2)||!pop(a)||!std::holds_alternative<std::int32_t>(a)) return fail("anewarray operand error");
            const auto ci=read_u2(code,f.pc);f.pc+=2;const auto component=cp_class_name(*f.cls,ci);const auto length=std::get<std::int32_t>(a);
            if(component.empty()) return fail("invalid anewarray component");
            if(length<0) {
                if(!raise_exception(start,"java/lang/NegativeArraySizeException",std::to_string(length))) return result_;
                ++executed; ++total_instructions_; continue;
            }
            const auto ref=heap_.allocate_reference_array(static_cast<std::size_t>(length),component);if(!ref)return fail("anewarray allocation failed");ok=push(ref);
        }
        else if(op==0xb2 || op==0xb3) { // getstatic / putstatic
            if(!need(2))return fail("truncated static field opcode");
            const auto ci=read_u2(code,f.pc);f.pc+=2;std::string owner,name,desc;
            if(!cp_member(*f.cls,ci,owner,name,desc,9,9))return fail("invalid static field reference");
            const auto* target_class=classes_.find_class(owner);
            const bool initialized=std::find(initialized_classes_.begin(),initialized_classes_.end(),owner)!=initialized_classes_.end();
            const bool initializing=std::find(initializing_classes_.begin(),initializing_classes_.end(),owner)!=initializing_classes_.end();
            if(target_class&&!initialized&&!initializing) {
                // JVM initializes the superclass before the class that owns the
                // active static field. Each <clinit> is a normal resumable frame.
                const ParsedClassCode* init_class=target_class;
                std::vector<const ParsedClassCode*> hierarchy;
                const ParsedClassCode* cursor=target_class;
                while(cursor) {
                    hierarchy.push_back(cursor);
                    cursor=cursor->super_class.empty()?nullptr:classes_.find_class(cursor->super_class);
                }
                for(auto it=hierarchy.rbegin();it!=hierarchy.rend();++it) {
                    const auto& candidate=(*it)->this_class;
                    const bool candidate_initialized=std::find(initialized_classes_.begin(),initialized_classes_.end(),candidate)!=initialized_classes_.end();
                    const bool candidate_initializing=std::find(initializing_classes_.begin(),initializing_classes_.end(),candidate)!=initializing_classes_.end();
                    if(!candidate_initialized&&!candidate_initializing){init_class=*it;break;}
                }
                if(!initialize_static_storage(*init_class))return fail("static field default initialization failed");
                initializing_classes_.push_back(init_class->this_class);
                auto clinit=classes_.resolve_method(init_class->this_class,"<clinit>","()V",InvokeKind::Static);
                if(clinit.method) {
                    f.pc=start;
                    if(!push_frame(clinit,0,{}))return fail("cannot create class initializer frame");
                    frames_.back().initializer_class=init_class->this_class;
                    ++executed; ++total_instructions_; continue;
                }
                initializing_classes_.erase(std::remove(initializing_classes_.begin(),initializing_classes_.end(),init_class->this_class),initializing_classes_.end());
                initialized_classes_.push_back(init_class->this_class);
                if(init_class!=target_class){f.pc=start;++executed;++total_instructions_;continue;}
            }
            if(op==0xb2) { JvmValue value; if(!statics_.get(owner,name,value)) {
                if(desc=="I"||desc=="Z"||desc=="B"||desc=="C"||desc=="S") value=std::int32_t(0);
                else if(desc=="F") value=0.0f; else if(desc=="J") value=std::int64_t(0); else if(desc=="D") value=0.0; else value=NullValue{};
                statics_.define(owner,name,value);
            } ok=push(value); }
            else { if(!pop(a))return fail("putstatic stack underflow"); if(!statics_.set(owner,name,a)) statics_.define(owner,name,a); }
        }
        else if(op==0xc0 || op==0xc1) { // checkcast / instanceof
            if(!need(2)||!pop(a)) return fail("type-check stack/operand error");
            const auto ci=read_u2(code,f.pc);
            f.pc+=2;
            const auto target=cp_class_name(*f.cls,ci);
            if(target.empty()) return fail("invalid type-check class");
            if(std::holds_alternative<NullValue>(a)) { if(op==0xc0)ok=push(a); else ok=push(std::int32_t(0)); }
            else if(!std::holds_alternative<ObjectRef>(a))return fail("type-check requires reference");
            else { const auto ref=std::get<ObjectRef>(a);const auto*object=heap_.get(ref);if(!object)return fail("type-check invalid reference");const bool match=classes_.is_assignable(object->class_name,target);if(op==0xc0){if(!match){if(!raise_exception(start,"java/lang/ClassCastException",object->class_name+" to "+target))return result_;++executed;++total_instructions_;continue;}ok=push(a);}else ok=push(std::int32_t(match?1:0)); }
        }
        else if(op==0xb4||op==0xb5) {if(!need(2))return fail("truncated field opcode");auto ci=read_u2(code,f.pc);f.pc+=2;std::string owner,name,desc;if(!cp_member(*f.cls,ci,owner,name,desc,9,9))return fail("invalid field reference");JvmValue recv; if(op==0xb4){if(!pop(recv))return fail("getfield stack underflow");}else{if(!pop(a)||!pop(recv))return fail("putfield stack underflow");}if(std::holds_alternative<NullValue>(recv)){if(!raise_exception(start,"java/lang/NullPointerException","field access on null"))return result_;++executed;++total_instructions_;continue;}if(!std::holds_alternative<ObjectRef>(recv)||!heap_.valid_ref(std::get<ObjectRef>(recv)))return fail("invalid field receiver");auto ref=std::get<ObjectRef>(recv);const auto key=owner+"."+name;if(op==0xb4){JvmValue v;if(!heap_.get_field(ref,key,v))return fail("field not found: "+key);ok=push(v);}else ok=heap_.put_field(ref,key,a);}
        else if(op==0xbb) {if(!need(2))return fail("truncated new");auto ci=read_u2(code,f.pc);f.pc+=2;auto owner=cp_class_name(*f.cls,ci);std::vector<FieldLayout> layout;std::string err;if(owner.empty()||!classes_.build_instance_layout(owner,layout,err))return fail("cannot allocate class: "+(err.empty()?owner:err));auto ref=heap_.allocate_instance(owner,layout);if(!ref)return fail("heap allocation failed");ok=push(ref);}
        else if(op>=0xb6&&op<=0xb8) {if(!need(2))return fail("truncated invoke");auto ci=read_u2(code,f.pc);f.pc+=2;std::string owner,name,desc;if(!cp_member(*f.cls,ci,owner,name,desc,10,11))return fail("invalid method reference");std::vector<char> arg_types;char ret=0;if(!parse_args(desc,arg_types,ret))return fail("unsupported method descriptor: "+desc);std::vector<JvmValue> args(arg_types.size());for(std::size_t i=args.size();i>0;--i){if(!pop(args[i-1]))return fail("invoke argument stack underflow");}ObjectRef receiver=0;if(op!=0xb8){if(!pop(a))return fail("invoke receiver stack underflow");if(std::holds_alternative<NullValue>(a)){if(!raise_exception(start,"java/lang/NullPointerException","invoke on null"))return result_;++executed;++total_instructions_;continue;}if(!std::holds_alternative<ObjectRef>(a))return fail("non-object invoke receiver");receiver=std::get<ObjectRef>(a);if(!heap_.valid_ref(receiver))return fail("invalid invoke receiver");}
            InvokeKind kind=op==0xb8?InvokeKind::Static:(op==0xb7?InvokeKind::Special:InvokeKind::Virtual);auto actual=receiver?heap_.get(receiver)->class_name:owner;auto resolved=classes_.resolve_method(owner,name,desc,kind,actual);if(!resolved.method)return fail("method resolution failed: "+owner+"."+name+desc);f.call_site_pc=start;if(!push_frame(resolved,receiver,args))return fail("cannot create callee frame");}
        else if(op==0xbf) { // athrow; null is specified to throw NPE
            if(!pop(a)) return fail("athrow stack underflow");
            if(std::holds_alternative<NullValue>(a)){if(!raise_exception(start,"java/lang/NullPointerException","athrow null"))return result_;}
            else if(!std::holds_alternative<ObjectRef>(a)) return fail("athrow requires reference");
            else {const auto exception=std::get<ObjectRef>(a);if(!heap_.valid_ref(exception))return fail("athrow stale reference");if(!propagate_exception(start,exception))return fail("uncaught Java exception: "+heap_.get(exception)->class_name);}
        }
        else if(op==0xac||op==0xb0||op==0xb1) {JvmValue ret=NullValue{};if(op!=0xb1&&!pop(ret))return fail("return stack underflow");const auto initializer=f.initializer_class;frames_.pop_back();if(!initializer.empty()){initializing_classes_.erase(std::remove(initializing_classes_.begin(),initializing_classes_.end(),initializer),initializing_classes_.end());if(std::find(initialized_classes_.begin(),initialized_classes_.end(),initializer)==initialized_classes_.end())initialized_classes_.push_back(initializer);}if(frames_.empty()){result_.status=Status::Completed;result_.value=ret;}else if(op!=0xb1){auto& caller=frames_.back();if(caller.stack.size()>=caller.method->max_stack)return fail("caller operand stack overflow");caller.stack.push_back(ret);}}
        else return fail("unsupported opcode "+std::to_string(op)+" at "+std::to_string(start));
        if (!ok) return fail("invalid operand/local operation");
        ++executed;
        ++total_instructions_;
    }
    result_.instructions_executed=total_instructions_;
    if(frames_.empty()&&result_.status!=Status::Completed)result_.status=Status::Completed;
    else if(result_.status!=Status::Completed)result_.status=Status::Yielded;
    return result_;
}

JvmResumableObjectInterpreter::Result JvmResumableObjectInterpreter::run_to_completion(std::size_t quantum) {
    if (!quantum) quantum = 1;
    while (result_.status != Status::Completed && result_.status != Status::Failed) step(quantum);
    return result_;
}
std::vector<JvmResumableObjectInterpreter::FrameView> JvmResumableObjectInterpreter::frames() const {
    std::vector<FrameView> out;out.reserve(frames_.size());for(const auto& f:frames_)out.push_back({f.cls->this_class,f.method->name,f.method->descriptor,f.pc,f.receiver,f.call_site_pc,f.initializer_class,f.locals,f.stack});return out;
}
JvmResumableObjectInterpreter::Snapshot JvmResumableObjectInterpreter::checkpoint() const {
    Snapshot snapshot; snapshot.status=result_.status; snapshot.value=result_.value;
    snapshot.total_instructions=total_instructions_; snapshot.error=result_.error;
    snapshot.initialized_classes=initialized_classes_; snapshot.initializing_classes=initializing_classes_;
    snapshot.frames.reserve(frames_.size());
    for(const auto& frame:frames_) snapshot.frames.push_back({frame.cls->this_class,frame.method->name,frame.method->descriptor,frame.pc,frame.receiver,frame.call_site_pc,frame.initializer_class,frame.locals,frame.stack});
    return snapshot;
}
bool JvmResumableObjectInterpreter::restore(const Snapshot& snapshot, std::string& error) {
    std::vector<Frame> restored; restored.reserve(snapshot.frames.size());
    if(snapshot.frames.size()>max_call_depth_){error="snapshot exceeds maximum call depth";return false;}
    for(const auto& saved:snapshot.frames){
        const auto* cls=classes_.find_class(saved.class_name); if(!cls){error="snapshot class is not loaded: "+saved.class_name;return false;}
        const MethodCode* method=nullptr; for(const auto& candidate:cls->methods) if(candidate.name==saved.method_name&&candidate.descriptor==saved.descriptor){method=&candidate;break;}
        if(!method){error="snapshot method is not loaded: "+saved.class_name+"."+saved.method_name;return false;}
        if(saved.pc>method->code.size()||saved.call_site_pc>65535||saved.locals.size()!=method->max_locals||saved.operand_stack.size()>method->max_stack){error="snapshot frame bounds are invalid";return false;}
        if(saved.receiver&&!heap_.valid_ref(saved.receiver)){error="snapshot receiver is no longer valid";return false;}
        for(const auto& value:saved.locals) if(const auto* ref=std::get_if<ObjectRef>(&value);ref&&*ref&&!heap_.valid_ref(*ref)){error="snapshot local contains stale object reference";return false;}
        for(const auto& value:saved.operand_stack) if(const auto* ref=std::get_if<ObjectRef>(&value);ref&&*ref&&!heap_.valid_ref(*ref)){error="snapshot stack contains stale object reference";return false;}
        Frame frame;frame.cls=cls;frame.method=method;frame.pc=saved.pc;frame.receiver=saved.receiver;frame.call_site_pc=saved.call_site_pc;frame.initializer_class=saved.initializer_class;frame.locals=saved.locals;frame.stack=saved.operand_stack;restored.push_back(std::move(frame));
    }
    frames_=std::move(restored);total_instructions_=snapshot.total_instructions;initialized_classes_=snapshot.initialized_classes;initializing_classes_=snapshot.initializing_classes;result_.status=snapshot.status;result_.value=snapshot.value;result_.error=snapshot.error;result_.instructions_executed=total_instructions_;error.clear();return true;
}
void JvmResumableObjectInterpreter::reset(){frames_.clear();result_=Result{};total_instructions_=0;initializing_classes_.clear();}
} // namespace opti::runtime
