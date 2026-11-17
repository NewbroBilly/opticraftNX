#include "java_thread_scheduler.hpp"
#include <cassert>
#include <stdexcept>
using namespace opti::runtime;
int main(){
 JavaThreadScheduler s(3); int a=0,b=0;
 auto x=s.create("Render",[&]{++a; return a<3;});
 auto y=s.create("Sound",[&]{++b; return false;},true);
 assert(x&&y); assert(!s.is_alive(x)); assert(s.start(x)); assert(s.start(y));
 assert(!s.start(x)); assert(s.run_ready(1)==1); assert(a+b==1);
 assert(s.run_ready(8)==3); assert(a==3&&b==1);
 assert(s.state(x)==JavaThreadScheduler::State::Terminated);
 assert(s.state(y)==JavaThreadScheduler::State::Terminated);
 auto z=s.create("Failure",[]{throw std::runtime_error("x"); return false;}); assert(z&&s.start(z));
 assert(s.run_ready(1)==1); assert(s.state(z)==JavaThreadScheduler::State::Failed);
 assert(s.create("overflow",[]{return false;})==0);
}
