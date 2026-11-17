#include "java_runtime_bootstrap.hpp"
#include "class_code_parser.hpp"
#include "jvm_class_runtime.hpp"
#include "jvm_object_interpreter.hpp"
#include "lwjgl_gl11_bridge.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdlib>
#include <limits>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <unordered_set>
using namespace opti::runtime;
namespace {
struct HostWindow { int creates=0, visibility_calls=0, width=0, height=0, presents=0; bool visible=false; std::string title; std::vector<std::uint32_t> pixels; };
bool host_create(void* user,const char* title,int width,int height){auto& h=*static_cast<HostWindow*>(user);++h.creates;h.title=title?title:"";h.width=width;h.height=height;return true;}
bool host_visible(void* user,bool visible){auto& h=*static_cast<HostWindow*>(user);++h.visibility_calls;h.visible=visible;return true;}
}
int main(int argc,char**argv){
 if(argc<2 || argc>4){std::cerr<<"usage: run_original_minecraft_main_probe minecraft.jar [instruction_budget] [user_home]\n";return 2;}
 std::size_t instruction_budget = 1000000;
 if (argc >= 3) {
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(argv[2], &end, 10);
  if (!end || *end != '\0' || parsed == 0 || parsed > std::numeric_limits<std::size_t>::max()) {
   std::cerr << "invalid instruction budget\n"; return 2;
  }
  instruction_budget = static_cast<std::size_t>(parsed);
 }
 std::ifstream file(argv[1],std::ios::binary);
 if(!file){std::cerr<<"cannot open minecraft.jar\n";return 2;}
 const std::vector<std::uint8_t> jar((std::istreambuf_iterator<char>(file)),{});
 const auto inspection=inspect_jar(jar.data(),jar.size(),true);
 if(!inspection.valid){std::cerr<<"JAR inspection failed: "<<inspection.error<<"\n";return 3;}
 JvmClassRuntime classes;std::size_t registered=0;
 std::size_t resources_registered=0;
 for(const auto&entry:inspection.entries){
  std::vector<std::uint8_t> bytes;std::string error;
  if(!extract_jar_entry(jar.data(),jar.size(),entry,bytes,&error)){std::cerr<<entry.name<<": "<<error<<"\n";return 4;}
  if(entry.name.size()>=6&&entry.name.compare(entry.name.size()-6,6,".class")==0){
   auto parsed=parse_class_code(bytes.data(),bytes.size());
   if(!parsed.valid){std::cerr<<entry.name<<": "<<parsed.error<<"\n";return 5;}
   const auto result=classes.register_class(std::move(parsed));
   if(!result.success){std::cerr<<entry.name<<": "<<result.error<<"\n";return 6;}
   ++registered;
  } else if(!entry.name.empty()&&entry.name.back()!='/'){
   if(!classes.register_resource(entry.name,std::move(bytes))){std::cerr<<entry.name<<": duplicate resource\n";return 6;}
   ++resources_registered;
  }
 }
 // The original game JAR references LWJGL/JInput classes but does not bundle
 // those libraries. Load the sibling runtime JARs from mcdata/bin as well.
 std::size_t library_classes_registered = 0;
 std::size_t library_resources_registered = 0;
 const std::filesystem::path jar_directory = std::filesystem::path(argv[1]).parent_path();
 std::vector<std::pair<std::string, std::filesystem::path>> runtime_archives = {
  {"lwjgl.jar", jar_directory / "lwjgl.jar"},
  {"lwjgl_util.jar", jar_directory / "lwjgl_util.jar"},
  {"jinput.jar", jar_directory / "jinput.jar"},
  {"rt.jar", jar_directory.parent_path() / "runtime" / "lib" / "rt.jar"}
 };
 for (const auto& runtime_archive : runtime_archives) {
  const auto& library_name = runtime_archive.first;
  const auto& library_path = runtime_archive.second;
  if (!std::filesystem::exists(library_path)) continue;
  std::ifstream library_file(library_path, std::ios::binary);
  if (!library_file) { std::cerr << "cannot open runtime library: " << library_path << "\n"; return 3; }
  const std::vector<std::uint8_t> library_bytes((std::istreambuf_iterator<char>(library_file)), {});
  const auto library_inspection = inspect_jar(library_bytes.data(), library_bytes.size(), false);
  if (!library_inspection.valid) { std::cerr << library_name << " inspection failed: " << library_inspection.error << "\n"; return 3; }
  for (const auto& entry : library_inspection.entries) {
   std::vector<std::uint8_t> bytes; std::string error;
   if (!extract_jar_entry(library_bytes.data(), library_bytes.size(), entry, bytes, &error)) {
    std::cerr << library_name << ":" << entry.name << ": " << error << "\n"; return 4;
   }
   if (entry.name.size() >= 6 && entry.name.compare(entry.name.size() - 6, 6, ".class") == 0) {
    auto parsed = parse_class_code(bytes.data(), bytes.size());
    if (!parsed.valid) { std::cerr << library_name << ":" << entry.name << ": " << parsed.error << "\n"; return 5; }
    if (classes.find_class(parsed.this_class)) continue; // Earlier application/runtime definition wins.
    const auto result = classes.register_class(std::move(parsed));
    if (!result.success) { std::cerr << library_name << ":" << entry.name << ": " << result.error << "\n"; return 6; }
    ++library_classes_registered;
   } else if (!entry.name.empty() && entry.name.back() != '/') {
    if (classes.register_resource(entry.name, std::move(bytes))) ++library_resources_registered;
   }
  }
 }
 const auto*main_class=classes.find_class("net/minecraft/client/Minecraft");
 if(!main_class||registered!=inspection.class_count){std::cerr<<"incomplete class registration\n";return 7;}
 JvmHeap heap;JvmStaticFields statics;
 if (argc >= 4 && argv[3][0] != '\0') {
  const ObjectRef user_home = heap.allocate_string(argv[3]);
  if (!user_home || !statics.define("opti/switchport/SystemProperties", "user.home", user_home)) {
   std::cerr << "cannot set user.home override\n"; return 2;
  }
 }
 HostWindow host;AwtHostCallbacks awt{&host,host_create,host_visible};
 opti::gl::LwjglGL11Bridge gl_bridge(854,480);
 gl_bridge.set_present_callback([&host](const std::uint32_t* pixels,int width,int height){
  if(!pixels||width<=0||height<=0)return;
  host.width=width;host.height=height;host.pixels.assign(pixels,pixels+static_cast<std::size_t>(width)*static_cast<std::size_t>(height));++host.presents;
 });
 ObjectBytecodeInterpreter vm(heap,statics,instruction_budget,&classes,&awt,&gl_bridge);
 const auto argv_ref=heap.allocate_reference_array(0,"Ljava/lang/String;");
 const auto execution=vm.invoke_static(*main_class,"main","([Ljava/lang/String;)V",{argv_ref});
 std::cout<<"JAR_CLASSES_REGISTERED="<<registered<<"\n";
 std::cout<<"JAR_RESOURCES_REGISTERED="<<resources_registered<<"\n";
 std::cout<<"RUNTIME_LIBRARY_CLASSES_REGISTERED="<<library_classes_registered<<"\n";
 std::cout<<"RUNTIME_LIBRARY_RESOURCES_REGISTERED="<<library_resources_registered<<"\n";
 std::cout<<"MAIN_EXECUTION="<<(execution.success?"RETURNED":"STOPPED")<<"\n";
 std::cout<<"INSTRUCTION_BUDGET="<<instruction_budget<<"\n";
 std::cout<<"INSTRUCTIONS_EXECUTED="<<execution.instructions_executed<<"\n";
 std::cout<<"DISPLAY_PRESENTS="<<host.presents<<"\n";
 if (!vm.diagnostics().empty()) {
  std::cout << "JAVA_DIAGNOSTICS_BEGIN\n" << vm.diagnostics() << "JAVA_DIAGNOSTICS_END\n";
 }
 const auto& completed_frames = vm.execution_frames().completed_frames();
 const std::size_t history_start = completed_frames.size() > 24 ? completed_frames.size() - 24 : 0;
 for (std::size_t i = history_start; i < completed_frames.size(); ++i) {
  const auto& frame = completed_frames[i];
  std::cout << "RECENT_CALL=" << frame.class_name << "." << frame.method_name
            << frame.descriptor << " pc=" << frame.pc << "\n";
 }
 if(host.presents>0 && !host.pixels.empty()){
  std::filesystem::create_directories("tests/build");
  std::ofstream ppm("tests/build/original_minecraft_frame.ppm",std::ios::binary);
  ppm<<"P6\n"<<host.width<<" "<<host.height<<"\n255\n";
  std::unordered_set<std::uint32_t> unique_pixels;
  std::size_t non_black=0;
  for(const auto pixel:host.pixels){unique_pixels.insert(pixel);if((pixel&0x00ffffffu)!=0)++non_black;const char rgb[3]={static_cast<char>(pixel&0xffu),static_cast<char>((pixel>>8)&0xffu),static_cast<char>((pixel>>16)&0xffu)};ppm.write(rgb,3);}
  ppm.close();
  std::cout<<"FIRST_FRAME_CAPTURE=tests/build/original_minecraft_frame.ppm\n";
  std::cout<<"FRAME_UNIQUE_PIXELS="<<unique_pixels.size()<<"\n";
  std::cout<<"FRAME_NON_BLACK_PIXELS="<<non_black<<"\n";
  std::cout<<"FRAME_CENTER_RGBA=0x"<<std::hex<<host.pixels[(static_cast<std::size_t>(host.height)/2u)*static_cast<std::size_t>(host.width)+static_cast<std::size_t>(host.width)/2u]<<std::dec<<"\n";
  if(unique_pixels.size()>1 && non_black>0) {
   std::cout<<"ORIGINAL_MINECRAFT_FIRST_FRAME=PASS\n";
  }
 }
 if(execution.success){std::cout<<"MAIN_RETURNED\n";std::cout<<"AWT_WINDOW_CREATES="<<host.creates<<"\n";std::cout<<"AWT_WINDOW_TITLE="<<host.title<<"\n";std::cout<<"AWT_WINDOW_SIZE="<<host.width<<"x"<<host.height<<"\n";std::cout<<"AWT_WINDOW_VISIBLE="<<(host.visible?"true":"false")<<"\n";if(host.creates==1&&host.visible&&host.width>0&&host.height>0){std::cout<<"ORIGINAL_MAIN_AWT_HOST_HANDOFF=PASS\n";return 0;}std::cerr<<"Java main returned without creating and showing a host window\n";return 9;}
 std::cout<<"FIRST_UNSUPPORTED_BOUNDARY="<<execution.error<<"\n";
 for (const auto& frame : vm.execution_frames().frames()) {
  std::cout << "CALL_FRAME=" << frame.class_name << "." << frame.method_name << frame.descriptor
            << " pc=" << frame.pc << "\n";
 }
 if(execution.error.find("java/awt/Frame.<init>(Ljava/lang/String;)V")!=std::string::npos){
  std::cout<<"PROBE_OK: original Minecraft.main reached AWT window creation; AWT-to-Switch adapter is the next blocker.\n";return 0;
 }
 std::cerr<<"Probe stopped before expected AWT boundary.\n";return 8;
}
