#include "java_thread_scheduler.hpp"
#include <utility>

namespace opti::runtime {
JavaThreadScheduler::Thread* JavaThreadScheduler::find(ThreadId id) { for (auto& t:threads_) if(t.id==id) return &t; return nullptr; }
const JavaThreadScheduler::Thread* JavaThreadScheduler::find(ThreadId id) const { for (const auto& t:threads_) if(t.id==id) return &t; return nullptr; }
JavaThreadScheduler::ThreadId JavaThreadScheduler::create(std::string name, Step step, bool daemon) {
    if (!step || threads_.size()>=max_threads_ || next_id_==0) return 0;
    const auto id=next_id_++; threads_.push_back(Thread{id,std::move(name),std::move(step),State::New,daemon,false,0}); return id;
}
bool JavaThreadScheduler::start(ThreadId id) { auto* t=find(id); if(!t || t->state!=State::New) return false; t->state=State::Runnable; return true; }
std::size_t JavaThreadScheduler::run_ready(std::size_t max_steps) {
    std::size_t ran=0, examined=0;
    while(ran<max_steps && !threads_.empty() && examined<threads_.size()) {
        if(cursor_>=threads_.size()) cursor_=0;
        auto& t=threads_[cursor_++]; ++examined;
        if(t.state!=State::Runnable) continue;
        t.state=State::Running;
        try { const bool yielded=t.step(); ++t.steps; ++ran; if (t.state != State::Parked) t.state=yielded?State::Runnable:State::Terminated; }
        catch(...) { ++t.steps; ++ran; t.state=State::Failed; }
        examined=0;
    }
    return ran;
}
bool JavaThreadScheduler::interrupt(ThreadId id) { auto* t=find(id); if(!t || t->state==State::Terminated || t->state==State::Failed) return false; t->interrupted=true; return true; }
bool JavaThreadScheduler::park(ThreadId id) { auto* t=find(id); if(!t || (t->state!=State::Runnable && t->state!=State::Running)) return false; t->state=State::Parked; return true; }
bool JavaThreadScheduler::resume(ThreadId id) { auto* t=find(id); if(!t || t->state!=State::Parked) return false; t->state=State::Runnable; return true; }
bool JavaThreadScheduler::interrupted(ThreadId id, bool clear) { auto* t=find(id); if(!t) return false; const bool value=t->interrupted; if(clear) t->interrupted=false; return value; }
bool JavaThreadScheduler::is_alive(ThreadId id) const { const auto* t=find(id); return t && (t->state==State::Runnable || t->state==State::Running || t->state==State::Parked); }
JavaThreadScheduler::State JavaThreadScheduler::state(ThreadId id) const { const auto* t=find(id); return t?t->state:State::Failed; }
std::vector<JavaThreadScheduler::Snapshot> JavaThreadScheduler::snapshot() const { std::vector<Snapshot> out; out.reserve(threads_.size()); for(const auto& t:threads_) out.push_back({t.id,t.name,t.state,t.steps}); return out; }
} // namespace opti::runtime
