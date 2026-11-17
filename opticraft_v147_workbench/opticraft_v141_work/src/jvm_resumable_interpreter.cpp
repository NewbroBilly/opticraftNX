#include "jvm_resumable_interpreter.hpp"
#include <climits>
#include <cstring>
#include <limits>

namespace opti::runtime {
namespace {
std::uint16_t u2(const std::vector<std::uint8_t>& c, std::size_t p) {
    return (std::uint16_t(c[p]) << 8) | c[p + 1];
}
std::int16_t s2(const std::vector<std::uint8_t>& c, std::size_t p) { return static_cast<std::int16_t>(u2(c,p)); }
std::int32_t s4(const std::vector<std::uint8_t>& c, std::size_t p) {
    const std::uint32_t v=(std::uint32_t(c[p])<<24)|(std::uint32_t(c[p+1])<<16)|(std::uint32_t(c[p+2])<<8)|c[p+3];
    std::int32_t out; std::memcpy(&out,&v,sizeof(out)); return out;
}
std::int32_t wrap(std::uint32_t v) { std::int32_t out; std::memcpy(&out,&v,sizeof(out)); return out; }
bool int_descriptor(const std::string& d, std::size_t& n) {
    n=0; if(d.empty()||d[0]!='(') return false; std::size_t p=1;
    while(p<d.size()&&d[p]!=')') { if(d[p++]!='I') return false; ++n; }
    return p+2==d.size()&&d[p]==')'&&d[p+1]=='I';
}
const MethodCode* find_method(const ParsedClassCode& c,const std::string& n,const std::string& d) {
    for(const auto& m:c.methods) if(m.name==n&&m.descriptor==d) return &m;
    return nullptr;
}
}

void ResumableIntegerInterpreter::reset() { frames_.clear(); class_=nullptr; result_=Result{}; total_instructions_=0; }

bool ResumableIntegerInterpreter::begin(const ParsedClassCode& cls,const std::string& name,
 const std::string& desc,const std::vector<std::int32_t>& args) {
    reset();
    if(!cls.valid) { result_.status=Status::Failed; result_.error="classfile was not parsed successfully"; return false; }
    std::size_t argc=0; const auto* m=find_method(cls,name,desc);
    if(!m) { result_.status=Status::Failed; result_.error="method not found"; return false; }
    if(!(m->access_flags&0x0008)||!int_descriptor(desc,argc)||argc!=args.size()||args.size()>m->max_locals||m->code.empty()) {
        result_.status=Status::Failed; result_.error="unsupported method shape; expected static integer method"; return false;
    }
    class_=&cls;
    Frame f; f.cls=&cls; f.method=m; f.locals.resize(m->max_locals,0); f.stack.reserve(m->max_stack);
    for(std::size_t i=0;i<args.size();++i) f.locals[i]=args[i];
    frames_.push_back(std::move(f)); result_.status=Status::Ready; return true;
}

ResumableIntegerInterpreter::Result ResumableIntegerInterpreter::step(std::size_t quantum) {
    if(result_.status==Status::Completed||result_.status==Status::Failed) return result_;
    if(frames_.empty()) { result_.status=Status::Failed; result_.error="no active execution frame"; return result_; }
    if(quantum==0) { result_.status=Status::Yielded; return result_; }
    result_.status=Status::Running;
    std::size_t executed=0;
    auto fail=[&](const std::string& e){ result_.status=Status::Failed; result_.error=e; return result_; };
    while(executed<quantum&&!frames_.empty()) {
        Frame& f=frames_.back(); const auto& code=f.method->code;
        if(f.pc>=code.size()) return fail("method terminated without return");
        const std::size_t start=f.pc; const std::uint8_t op=code[f.pc++];
        auto need=[&](std::size_t n){return f.pc<=code.size()&&n<=code.size()-f.pc;};
        auto push=[&](std::int32_t v){if(f.stack.size()>=f.method->max_stack)return false;f.stack.push_back(v);return true;};
        auto pop=[&](std::int32_t& v){if(f.stack.empty())return false;v=f.stack.back();f.stack.pop_back();return true;};
        auto load=[&](std::size_t i){return i<f.locals.size()&&push(f.locals[i]);};
        auto store=[&](std::size_t i){std::int32_t v;if(i>=f.locals.size()||!pop(v))return false;f.locals[i]=v;return true;};
        auto branch=[&](std::int32_t d){const std::int64_t t=std::int64_t(start)+d;if(t<0||t>=std::int64_t(code.size()))return false;f.pc=static_cast<std::size_t>(t);return true;};
        std::int32_t a=0,b=0; std::size_t idx=0; bool ok=true;
        if(op==0x00) {}
        else if(op>=0x02&&op<=0x08) ok=push(static_cast<std::int32_t>(op)-3);
        else if(op==0x10) { if(!need(1))return fail("truncated bipush"); ok=push(static_cast<std::int8_t>(code[f.pc++])); }
        else if(op==0x11) { if(!need(2))return fail("truncated sipush"); auto v=s2(code,f.pc);f.pc+=2;ok=push(v); }
        else if(op==0x12||op==0x13) { std::uint16_t ci=0;if(op==0x12){if(!need(1))return fail("truncated ldc");ci=code[f.pc++];}else{if(!need(2))return fail("truncated ldc_w");ci=u2(code,f.pc);f.pc+=2;}if(!ci||ci>=f.cls->constant_pool.size()||f.cls->constant_pool[ci].tag!=3)return fail("ldc supports integer constants only");ok=push(f.cls->constant_pool[ci].integer); }
        else if(op==0x15||op==0x36) { if(!need(1))return fail("truncated local access");idx=code[f.pc++];ok=op==0x15?load(idx):store(idx); }
        else if(op>=0x1a&&op<=0x1d) ok=load(op-0x1a);
        else if(op>=0x3b&&op<=0x3e) ok=store(op-0x3b);
        else if(op==0x84) { if(!need(2))return fail("truncated iinc");idx=code[f.pc++];auto v=static_cast<std::int8_t>(code[f.pc++]);if(idx>=f.locals.size())return fail("iinc local out of range");f.locals[idx]=wrap(std::uint32_t(f.locals[idx])+std::uint32_t(std::int32_t(v))); }
        else if(op==0x57) ok=pop(a);
        else if(op==0x59) { if(f.stack.empty())ok=false;else ok=push(f.stack.back()); }
        else if(op==0x60||op==0x64||op==0x68||op==0x6c||op==0x70||op==0x7e||op==0x80||op==0x82||op==0x78||op==0x7a||op==0x7c) {
            if(!pop(b)||!pop(a)) return fail("integer operation stack underflow");
            std::int32_t v=0;
            switch(op){case 0x60:v=wrap(std::uint32_t(a)+std::uint32_t(b));break;case 0x64:v=wrap(std::uint32_t(a)-std::uint32_t(b));break;case 0x68:v=wrap(std::uint32_t(a)*std::uint32_t(b));break;case 0x6c:if(!b)return fail("integer division by zero");v=(a==INT_MIN&&b==-1)?INT_MIN:a/b;break;case 0x70:if(!b)return fail("integer remainder by zero");v=(a==INT_MIN&&b==-1)?0:a%b;break;case 0x7e:v=a&b;break;case 0x80:v=a|b;break;case 0x82:v=a^b;break;case 0x78:v=wrap(std::uint32_t(a)<<(b&31));break;case 0x7a:v=a>>(b&31);break;case 0x7c:v=static_cast<std::int32_t>(std::uint32_t(a)>>(b&31));break;}ok=push(v);
        }
        else if(op==0x74) { if(!pop(a))ok=false;else ok=push(wrap(0u-std::uint32_t(a))); }
        else if(op>=0x99&&op<=0x9e) { if(!need(2))return fail("truncated conditional branch");auto d=s2(code,f.pc);f.pc+=2;if(!pop(a))return fail("branch stack underflow");bool t=op==0x99?a==0:op==0x9a?a!=0:op==0x9b?a<0:op==0x9c?a>=0:op==0x9d?a>0:a<=0;if(t&&!branch(d))return fail("invalid branch target"); }
        else if(op>=0x9f&&op<=0xa4) { if(!need(2))return fail("truncated compare branch");auto d=s2(code,f.pc);f.pc+=2;if(!pop(b)||!pop(a))return fail("compare stack underflow");bool t=op==0x9f?a==b:op==0xa0?a!=b:op==0xa1?a<b:op==0xa2?a>=b:op==0xa3?a>b:a<=b;if(t&&!branch(d))return fail("invalid compare target"); }
        else if(op==0xa7||op==0xc8) { const std::size_t n=op==0xa7?2:4;if(!need(n))return fail("truncated goto");auto d=n==2?std::int32_t(s2(code,f.pc)):s4(code,f.pc);f.pc+=n;if(!branch(d))return fail("invalid goto target"); }
        else if(op==0xaa||op==0xab) { while(f.pc%4){if(!need(1))return fail("truncated switch padding");++f.pc;}if(!need(op==0xaa?12:8))return fail("truncated switch header");auto def=s4(code,f.pc);f.pc+=4;std::int32_t delta=def;if(!pop(a))return fail("switch stack underflow");if(op==0xaa){auto lo=s4(code,f.pc),hi=s4(code,f.pc+4);f.pc+=8;if(hi<lo||std::int64_t(hi)-lo>16380)return fail("invalid tableswitch range");auto n=std::size_t(std::int64_t(hi)-lo+1);if(!need(n*4))return fail("truncated tableswitch");if(a>=lo&&a<=hi)delta=s4(code,f.pc+std::size_t(a-lo)*4);f.pc+=n*4;}else{auto n=s4(code,f.pc);f.pc+=4;if(n<0||n>8191||!need(std::size_t(n)*8))return fail("invalid lookupswitch");for(std::int32_t i=0;i<n;++i){auto key=s4(code,f.pc),off=s4(code,f.pc+4);f.pc+=8;if(key==a)delta=off;}}if(!branch(delta))return fail("invalid switch target"); }
        else if(op==0xb8) { if(!need(2))return fail("truncated invokestatic");auto ci=u2(code,f.pc);f.pc+=2;if(!ci||ci>=f.cls->constant_pool.size())return fail("invalid invokestatic index");const auto& r=f.cls->constant_pool[ci];if((r.tag!=10&&r.tag!=11)||!r.a||!r.b||r.a>=f.cls->constant_pool.size()||r.b>=f.cls->constant_pool.size())return fail("invalid invokestatic reference");const auto& ce=f.cls->constant_pool[r.a];const auto& nt=f.cls->constant_pool[r.b];if(ce.tag!=7||nt.tag!=12||ce.a>=f.cls->constant_pool.size()||nt.a>=f.cls->constant_pool.size()||nt.b>=f.cls->constant_pool.size())return fail("invalid invokestatic structure");const auto& owner=f.cls->constant_pool[ce.a].utf8;const auto& name=f.cls->constant_pool[nt.a].utf8;const auto& desc=f.cls->constant_pool[nt.b].utf8;if(owner!=f.cls->this_class)return fail("cross-class invokestatic unsupported");std::size_t argc=0;if(!int_descriptor(desc,argc))return fail("invokestatic target must return int and accept ints");const auto* target=find_method(*f.cls,name,desc);if(!target||!(target->access_flags&8)||target->code.empty())return fail("invokestatic target unavailable");if(frames_.size()>=64)return fail("maximum call depth exceeded");if(argc>f.stack.size())return fail("invokestatic argument stack underflow");Frame child;child.cls=f.cls;child.method=target;child.locals.resize(target->max_locals,0);child.stack.reserve(target->max_stack);for(std::size_t i=argc;i>0;--i){child.locals[i-1]=f.stack.back();f.stack.pop_back();}if(argc>target->max_locals)return fail("invokestatic args exceed max_locals");frames_.push_back(std::move(child)); }
        else if(op==0xac) { if(!pop(a))return fail("ireturn stack underflow");frames_.pop_back();if(frames_.empty()){result_.status=Status::Completed;result_.value=a;}else{auto& caller=frames_.back();if(caller.stack.size()>=caller.method->max_stack)return fail("caller stack overflow after return");caller.stack.push_back(a);} }
        else return fail("unsupported opcode "+std::to_string(op)+" at bytecode offset "+std::to_string(start));
        if(!ok) return fail("invalid local/stack operation");
        ++executed; ++total_instructions_;
    }
    result_.instructions_executed=total_instructions_;
    if(result_.status!=Status::Completed) result_.status=Status::Yielded;
    return result_;
}

ResumableIntegerInterpreter::Result ResumableIntegerInterpreter::run_to_completion(std::size_t quantum) {
    if(quantum==0) quantum=1;
    while(result_.status!=Status::Completed&&result_.status!=Status::Failed) step(quantum);
    return result_;
}

ResumableIntegerInterpreter::Checkpoint ResumableIntegerInterpreter::checkpoint() const {
    Checkpoint saved; saved.total_instructions=total_instructions_; saved.frames.reserve(frames_.size());
    for(const auto& f:frames_) saved.frames.push_back({f.cls->this_class,f.method->name,f.method->descriptor,f.pc,f.locals,f.stack});
    return saved;
}

bool ResumableIntegerInterpreter::restore_interpreter_checkpoint(const Checkpoint& saved) {
    return class_ && restore(*class_,saved);
}

bool ResumableIntegerInterpreter::restore(const ParsedClassCode& cls,const Checkpoint& saved) {
    if(!cls.valid||saved.frames.size()>64) return false;
    std::vector<Frame> restored; restored.reserve(saved.frames.size());
    for(const auto& item:saved.frames) {
        if(item.class_name!=cls.this_class) return false;
        const auto* method=find_method(cls,item.method_name,item.descriptor);
        if(!method||item.pc>method->code.size()||item.locals.size()!=method->max_locals||item.operand_stack.size()>method->max_stack) return false;
        Frame f;f.cls=&cls;f.method=method;f.pc=item.pc;f.locals=item.locals;f.stack=item.operand_stack;restored.push_back(std::move(f));
    }
    class_=&cls; frames_=std::move(restored);total_instructions_=saved.total_instructions;
    result_=Result{};result_.status=frames_.empty()?Status::Completed:Status::Yielded;
    return true;
}

std::vector<ResumableIntegerInterpreter::FrameView> ResumableIntegerInterpreter::frames() const {
    std::vector<FrameView> out;out.reserve(frames_.size());
    for(const auto& f:frames_)out.push_back({f.cls->this_class,f.method->name,f.method->descriptor,f.pc,f.locals,f.stack});
    return out;
}

} // namespace opti::runtime
