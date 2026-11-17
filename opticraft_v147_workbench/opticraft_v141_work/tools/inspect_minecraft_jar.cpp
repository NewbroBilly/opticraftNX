#include "java_runtime_bootstrap.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc,char** argv) {
    if(argc!=2 && argc!=5) {
        std::cerr << "usage: inspect_minecraft_jar <minecraft.jar> [--extract <entry> <output>]\n";
        return 2;
    }
    if(argc==5 && std::string(argv[2])!="--extract") return 2;
    std::ifstream in(argv[1],std::ios::binary);
    if(!in){std::cerr<<"cannot open jar\n";return 2;}
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
    const auto report=opti::runtime::inspect_jar(bytes.data(),bytes.size());
    if(!report.valid){std::cerr<<"INVALID: "<<report.error<<"\n";return 1;}
    std::cout<<"VALID JAR\nentries="<<report.entries.size()
             <<" classes="<<report.class_count<<" resources="<<report.resource_count
             <<" minecraft_main="<<(report.has_minecraft_main?"yes":"no")<<"\n";
    if(argc==5) {
        const auto* entry=opti::runtime::find_jar_entry(report,argv[3]);
        if(!entry){std::cerr<<"entry not found: "<<argv[3]<<"\n";return 1;}
        std::vector<std::uint8_t> extracted;
        std::string error;
        if(!opti::runtime::extract_jar_entry(bytes.data(),bytes.size(),*entry,extracted,&error)) {
            std::cerr<<"EXTRACT FAILED: "<<error<<"\n";return 1;
        }
        std::ofstream out(argv[4],std::ios::binary);
        if(!out){std::cerr<<"cannot open output\n";return 2;}
        out.write(reinterpret_cast<const char*>(extracted.data()),static_cast<std::streamsize>(extracted.size()));
        if(!out){std::cerr<<"write failed\n";return 2;}
        std::cout<<"EXTRACTED "<<entry->name<<" bytes="<<extracted.size()<<" crc32=verified\n";
    }
    return 0;
}
