#include "runtime_classpath.hpp"
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

    ClasspathArchive app{"minecraft.jar",bytes.data(),bytes.size(),&jar};
    RuntimeClassPath cp;
    cp.application.push_back(app);

    auto loaded=load_class_bytes("net.minecraft.client.Minecraft",cp);
    assert(loaded.found);
    assert(loaded.source_role==ArchiveRole::Application);
    assert(loaded.role_index==0);
    auto parsed=inspect_classfile(loaded.bytes.data(),loaded.bytes.size());
    assert(parsed.valid && parsed.this_class=="net/minecraft/client/Minecraft");

    // Parent-first precedence: the same class in bootstrap must win over app.
    cp.bootstrap.push_back(app);
    auto parent=load_class_bytes("net.minecraft.client.Minecraft",cp);
    assert(parent.found && parent.source_role==ArchiveRole::Bootstrap);
    assert(parent.resolved.archive_index==0);

    // Platform wins over application when bootstrap has no matching class.
    RuntimeClassPath platform_first;
    platform_first.platform.push_back(app);
    platform_first.application.push_back(app);
    auto platform=load_class_bytes("net.minecraft.client.Minecraft",platform_first);
    assert(platform.found && platform.source_role==ArchiveRole::Platform);

    auto missing=load_class_bytes("java.lang.String",RuntimeClassPath{});
    assert(!missing.found);
    auto malformed=load_class_bytes("../evil",cp);
    assert(!malformed.found);

    std::vector<std::string> parts;
    std::string error;
    assert(split_classpath("romfs:/java/rt.jar;romfs:/java/ext.jar",';',parts,&error));
    assert(parts.size()==2 && parts[0]=="romfs:/java/rt.jar");
    assert(!split_classpath("a;;b",';',parts,&error) && parts.empty());
    assert(!split_classpath("",':',parts,&error) && parts.empty());

    std::cout<<"PASS: parent-first bootstrap/platform/application order, classpath parsing and extraction\\n";
}
