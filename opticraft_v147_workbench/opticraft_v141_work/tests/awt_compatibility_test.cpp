#include "jvm_object_interpreter.hpp"
#include <cassert>
#include <iostream>
using namespace opti::runtime;
namespace {
std::uint16_t utf(ParsedClassCode& c,const char* s){ConstantPoolEntry e;e.tag=1;e.utf8=s;c.constant_pool.push_back(e);return static_cast<std::uint16_t>(c.constant_pool.size()-1);}
std::uint16_t cls(ParsedClassCode& c,const char* s){auto n=utf(c,s);ConstantPoolEntry e;e.tag=7;e.a=n;c.constant_pool.push_back(e);return static_cast<std::uint16_t>(c.constant_pool.size()-1);}
std::uint16_t nat(ParsedClassCode& c,const char* n,const char* d){auto a=utf(c,n),b=utf(c,d);ConstantPoolEntry e;e.tag=12;e.a=a;e.b=b;c.constant_pool.push_back(e);return static_cast<std::uint16_t>(c.constant_pool.size()-1);}
std::uint16_t ref(ParsedClassCode& c,std::uint16_t owner,std::uint16_t nt){ConstantPoolEntry e;e.tag=10;e.a=owner;e.b=nt;c.constant_pool.push_back(e);return static_cast<std::uint16_t>(c.constant_pool.size()-1);}
void emit(std::vector<std::uint8_t>& b,std::uint8_t op,std::uint16_t index){b.push_back(op);b.push_back(static_cast<std::uint8_t>(index>>8));b.push_back(static_cast<std::uint8_t>(index));}
struct Host { int creates=0, visibility_calls=0, width=0, height=0; bool visible=false; std::string title; };
bool create(void* p,const char* title,int w,int h){auto&v=*static_cast<Host*>(p);++v.creates;v.title=title;v.width=w;v.height=h;return true;}
bool visible(void* p,bool value){auto&v=*static_cast<Host*>(p);++v.visibility_calls;v.visible=value;return true;}
}
int main(){
 ParsedClassCode c;c.valid=true;c.this_class="test/AwtBoot";c.constant_pool.resize(1);
 const auto frame=cls(c,"java/awt/Frame"),canvas=cls(c,"java/awt/Canvas"),layout=cls(c,"java/awt/BorderLayout"),dimension=cls(c,"java/awt/Dimension");
 const auto frame_init=ref(c,frame,nat(c,"<init>","(Ljava/lang/String;)V"));
 const auto canvas_init=ref(c,canvas,nat(c,"<init>","()V"));
 const auto layout_init=ref(c,layout,nat(c,"<init>","()V"));
 const auto set_layout=ref(c,frame,nat(c,"setLayout","(Ljava/awt/LayoutManager;)V"));
 const auto add=ref(c,frame,nat(c,"add","(Ljava/awt/Component;Ljava/lang/Object;)V"));
 const auto dim_init=ref(c,dimension,nat(c,"<init>","(II)V"));
 const auto preferred=ref(c,canvas,nat(c,"setPreferredSize","(Ljava/awt/Dimension;)V"));
 const auto pack=ref(c,frame,nat(c,"pack","()V"));
 const auto set_visible=ref(c,frame,nat(c,"setVisible","(Z)V"));
 const auto title_utf=utf(c,"Minecraft");ConstantPoolEntry title;title.tag=8;title.a=title_utf;c.constant_pool.push_back(title);const auto title_index=static_cast<std::uint16_t>(c.constant_pool.size()-1);
 const auto center_utf=utf(c,"Center");ConstantPoolEntry center;center.tag=8;center.a=center_utf;c.constant_pool.push_back(center);const auto center_index=static_cast<std::uint16_t>(c.constant_pool.size()-1);
 MethodCode m;m.access_flags=8;m.name="boot";m.descriptor="()V";m.max_stack=6;m.max_locals=2;
 emit(m.code,0xbb,frame);m.code.push_back(0x59);m.code.push_back(0x12);m.code.push_back(static_cast<std::uint8_t>(title_index));emit(m.code,0xb7,frame_init);m.code.push_back(0x4b);
 emit(m.code,0xbb,canvas);m.code.push_back(0x59);emit(m.code,0xb7,canvas_init);m.code.push_back(0x4c);
 m.code.push_back(0x2a);emit(m.code,0xbb,layout);m.code.push_back(0x59);emit(m.code,0xb7,layout_init);emit(m.code,0xb6,set_layout);
 m.code.push_back(0x2a);m.code.push_back(0x2b);m.code.push_back(0x12);m.code.push_back(static_cast<std::uint8_t>(center_index));emit(m.code,0xb6,add);
 m.code.push_back(0x2b);emit(m.code,0xbb,dimension);m.code.push_back(0x59);m.code.push_back(0x11);m.code.push_back(0x03);m.code.push_back(0x56);m.code.push_back(0x11);m.code.push_back(0x01);m.code.push_back(0xe0);emit(m.code,0xb7,dim_init);emit(m.code,0xb6,preferred);
 m.code.push_back(0x2a);emit(m.code,0xb6,pack);m.code.push_back(0x2a);m.code.push_back(0x04);emit(m.code,0xb6,set_visible);m.code.push_back(0xb1);c.methods.push_back(m);
 JvmHeap heap;JvmStaticFields statics;Host host;AwtHostCallbacks callbacks{&host,create,visible};ObjectBytecodeInterpreter vm(heap,statics,10000,nullptr,&callbacks);
 const auto result=vm.invoke_static(c,"boot","()V",{});if(!result.success)std::cerr<<result.error<<"\n";
 assert(result.success);assert(host.creates==1&&host.title=="Minecraft");assert(host.width==854&&host.height==480);assert(host.visibility_calls==1&&host.visible);
 std::cout<<"AWT compatibility: Frame/Canvas/BorderLayout/Dimension -> host window callbacks PASS\n";
}
