#include "java_runtime_bootstrap.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using namespace opti::runtime;

struct Fake {
    std::vector<std::string> files;
    bool vm_ok=true,main_ok=true,vm_called=false,main_called=false;
    std::string classpath,main_class;
    std::vector<std::string> args;
};
static bool exists(void* u,const char* path) {
    auto& f=*static_cast<Fake*>(u);
    for(const auto& x:f.files)if(x==path)return true;
    return false;
}
static bool load_vm(void* u,const char* cp,const char* main) {
    auto& f=*static_cast<Fake*>(u);f.vm_called=true;f.classpath=cp;f.main_class=main;return f.vm_ok;
}
static bool invoke(void* u,const char* main,int argc,const char* const* argv) {
    auto& f=*static_cast<Fake*>(u);f.main_called=true;f.main_class=main;
    for(int i=0;i<argc;i++)f.args.emplace_back(argv[i]);
    return f.main_ok;
}
int main() {
    auto plan=make_launch_plan("sdmc:/switch/opticraft",{"--width","1280","--height","720"});
    assert(plan.valid);
    assert(plan.main_class=="net.minecraft.client.Minecraft");
    assert(plan.classpath.size()==5);
    assert(plan.classpath[0]=="sdmc:/switch/opticraft/minecraft.jar");
    assert(plan.joined_classpath.find("switchport.jar")!=std::string::npos);
    Fake fake;
    fake.files=plan.classpath;
    RuntimeCallbacks cb{&fake,exists,load_vm,invoke};
    auto result=bootstrap(plan,cb);
    assert(result.success&&fake.vm_called&&fake.main_called);
    assert(fake.args.size()==4&&fake.args[0]=="--width"&&fake.args[3]=="720");

    Fake missing;
    missing.files.assign(plan.classpath.begin(),plan.classpath.end()-1);
    auto failed=bootstrap(plan,RuntimeCallbacks{&missing,exists,load_vm,invoke});
    assert(!failed.success&&failed.failed_step=="classpath");
    assert(!missing.vm_called&&!missing.main_called);

    Fake badvm;badvm.files=plan.classpath;badvm.vm_ok=false;
    failed=bootstrap(plan,RuntimeCallbacks{&badvm,exists,load_vm,invoke});
    assert(!failed.success&&failed.failed_step=="load_vm"&&!badvm.main_called);

    Fake badmain;badmain.files=plan.classpath;badmain.main_ok=false;
    failed=bootstrap(plan,RuntimeCallbacks{&badmain,exists,load_vm,invoke});
    assert(!failed.success&&failed.failed_step=="invoke_main");

    assert(!make_launch_plan("").valid);
    assert(!make_launch_plan("sdmc:/bad;root").valid);
    assert(!make_launch_plan("sdmc:/bad/").valid);
    assert(!bootstrap(plan,RuntimeCallbacks{}).success);
    std::cout<<"java_runtime_bootstrap_test: PASS\\n";
}
