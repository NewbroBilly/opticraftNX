#include "classpath_resolver.hpp"
#include "classfile_inspector.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace opti::runtime;
int main(int argc,char** argv) {
    assert(argc==2);
    std::ifstream in(argv[1],std::ios::binary);
    assert(in.good());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
    auto jar=inspect_jar(bytes.data(),bytes.size());
    assert(jar.valid && jar.has_minecraft_main);
    ClasspathArchive cp{"minecraft.jar",bytes.data(),bytes.size(),&jar};
    std::vector<ClasspathArchive> path{cp};
    auto found=resolve_class("net.minecraft.client.Minecraft",path);
    assert(found.found && found.archive_index==0);
    assert(found.entry.name=="net/minecraft/client/Minecraft.class");
    std::vector<std::uint8_t> cls;
    std::string error;
    assert(read_resolved_class(found,path,cls,&error));
    auto parsed=inspect_classfile(cls.data(),cls.size());
    assert(parsed.valid && parsed.this_class=="net/minecraft/client/Minecraft");
    auto missing=resolve_class("java.lang.String",path);
    assert(!missing.found);
    auto malformed=resolve_class("../evil",path);
    assert(!malformed.found);
    auto invalid_archive=path;
    invalid_archive[0].inspection=nullptr;
    auto rejected=resolve_class("net.minecraft.client.Minecraft",invalid_archive);
    assert(!rejected.found);
    std::cout<<"PASS: ordered classpath resolution, extraction, validation, and misses\\n";
}
